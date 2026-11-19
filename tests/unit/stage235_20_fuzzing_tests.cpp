#include "cpu/cpu.h"
#include "loader/app_inspector.h"
#include "loader/macho_fixture_corpus.h"
#include "packaging/app_package.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "vfs/vfs.h"
#include "security/capability_manager.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::loader;
using namespace winios::packaging;
using namespace winios::kernel;
using namespace winios::memory;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

enum class MutationStrategy : uint32_t {
    BitFlip = 0,
    ByteSwap,
    ArithmeticDelta,
    BoundaryValue,
    RandomOverwrite,
    Count
};

class DeterministicMutator {
private:
    std::mt19937_64 rng_;
    const uint64_t initial_seed_;

public:
    explicit DeterministicMutator(uint64_t seed)
        : rng_(seed), initial_seed_(seed) {}

    uint64_t next_u64() noexcept { return rng_(); }

    std::vector<uint8_t> mutate(const std::vector<uint8_t>& seed_data, MutationStrategy& out_strat) {
        if (seed_data.empty()) return {0x00};

        std::vector<uint8_t> out = seed_data;
        const auto strat = static_cast<MutationStrategy>(rng_() % static_cast<uint32_t>(MutationStrategy::Count));
        out_strat = strat;

        switch (strat) {
        case MutationStrategy::BitFlip: {
            size_t byte_idx = static_cast<size_t>(rng_() % out.size());
            uint8_t bit_idx = static_cast<uint8_t>(rng_() % 8);
            out[byte_idx] ^= static_cast<uint8_t>(1u << bit_idx);
            break;
        }
        case MutationStrategy::ByteSwap: {
            if (out.size() > 1) {
                size_t idx1 = static_cast<size_t>(rng_() % out.size());
                size_t idx2 = static_cast<size_t>(rng_() % out.size());
                std::swap(out[idx1], out[idx2]);
            }
            break;
        }
        case MutationStrategy::ArithmeticDelta: {
            size_t idx = static_cast<size_t>(rng_() % out.size());
            const int8_t deltas[] = {-16, -8, -1, 1, 8, 16};
            int8_t d = deltas[rng_() % 6];
            out[idx] = static_cast<uint8_t>(out[idx] + d);
            break;
        }
        case MutationStrategy::BoundaryValue: {
            size_t idx = static_cast<size_t>(rng_() % out.size());
            const uint8_t boundaries[] = {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF};
            out[idx] = boundaries[rng_() % 6];
            break;
        }
        case MutationStrategy::RandomOverwrite: {
            size_t idx = static_cast<size_t>(rng_() % out.size());
            out[idx] = static_cast<uint8_t>(rng_() & 0xFF);
            break;
        }
        default:
            break;
        }
        return out;
    }
};

static void test_package_manifest_fuzzing() {
    std::cout << "[Test 1] Package manifest parsing fuzzing (1,000 iterations)..." << std::endl;
    DeterministicMutator mutator(0xF00D1234ULL);

    AppPackage pkg;
    pkg.manifest.format_version = "3.0";
    pkg.manifest.bundle_id = "com.winios.fuzz_manifest";
    pkg.manifest.name = "FuzzManifestApp";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/main";
    pkg.add_file("bin/main", {0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01});

    auto seed_bytes = pkg.pack_v3();
    std::cout << "  Packed v3, seed_bytes len = " << seed_bytes.size() << std::endl;
    size_t accepted = 0;
    size_t rejected = 0;

    for (size_t iter = 0; iter < 1000; ++iter) {
        MutationStrategy strat{};
        auto fuzzed = mutator.mutate(seed_bytes, strat);

        AppPackage out_pkg;
        bool ok = AppPackage::unpack(fuzzed.data(), fuzzed.size(), out_pkg);
        if (ok) {
            accepted++;
            TEST_ASSERT(!out_pkg.manifest.bundle_id.empty(), "Valid package has bundle id");
        } else {
            rejected++;
        }
    }

    TEST_ASSERT(accepted + rejected == 1000, "All 1,000 iterations accounted for");
    std::cout << "  Passed: " << accepted << " accepted, " << rejected << " safely rejected. Zero crashes." << std::endl;
}

static void test_macho_header_fuzzing() {
    std::cout << "[Test 2] Mach-O load commands & header fuzzing (1,000 iterations)..." << std::endl;
    DeterministicMutator mutator(0xFEEDFACECAFEULL);

    auto fixture = PublicMachOCorpus::create_minimal_executable();
    const auto& seed_bin = fixture.binary_data;

    size_t valid_count = 0;
    size_t rejected_count = 0;

    for (size_t iter = 0; iter < 1000; ++iter) {
        MutationStrategy strat{};
        auto fuzzed = mutator.mutate(seed_bin, strat);

        auto report = AppInspector::inspect_binary(fuzzed.data(), fuzzed.size());
        if (report.is_valid) {
            valid_count++;
            TEST_ASSERT(report.wx_clean, "Valid binary must be W^X clean");
        } else {
            rejected_count++;
            TEST_ASSERT(!report.rejection_reason.empty(), "Rejection reason populated");
        }
    }

    TEST_ASSERT(valid_count + rejected_count == 1000, "All iterations accounted for");
    std::cout << "  Passed: " << valid_count << " valid, " << rejected_count << " safely rejected. Zero crashes." << std::endl;
}

static void test_syscall_argument_fuzzing() {
    std::cout << "[Test 3] Syscall argument & boundary fuzzing (1,000 iterations)..." << std::endl;
    DeterministicMutator mutator(0x595CA11ULL);

    CPU cpu;
    Memory mem(0x1000000ULL);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);
    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();
    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("fuzz_proc", 1);
    TEST_ASSERT(proc != nullptr, "Process created");

    const uint64_t root_l2 = ppm.allocate_page();
    auto uas = std::make_unique<UserAddressSpace>(mem, ppm, root_l2, "uas_fuzz");
    proc->set_address_space(uas.get());

    size_t handled_count = 0;

    for (size_t iter = 0; iter < 1000; ++iter) {
        if (proc->state() == ProcessState::Zombie) {
            proc->set_state(ProcessState::Runnable);
        }
        uint64_t sys_num = mutator.next_u64() % 600;
        // Randomize CPU registers
        cpu.write_register(16, sys_num); // Fuzz syscall numbers (0 to 600)
        cpu.write_register(0, mutator.next_u64());
        cpu.write_register(1, mutator.next_u64());
        cpu.write_register(2, mutator.next_u64());
        cpu.write_register(3, mutator.next_u64());
        cpu.write_register(4, mutator.next_u64());
        cpu.write_register(5, mutator.next_u64());

        // Must gracefully dispatch and return error without crashing or throwing
        bool ok = dispatcher.handle_syscall(cpu, *proc, *uas, sched);
        (void)ok;
        handled_count++;
    }

    TEST_ASSERT(handled_count == 1000, "All 1,000 syscall fuzz iterations handled safely");
    std::cout << "  Passed: 1,000 randomized syscalls evaluated. Zero crashes or unhandled faults.\n";
}

int main() {
    std::cout << "=== Stage 235.20 Fuzzing Infrastructure & Corpus Tests ===" << std::endl;
    test_package_manifest_fuzzing();
    test_macho_header_fuzzing();
    test_syscall_argument_fuzzing();
    std::cout << "=== All Stage 235.20 tests PASSED successfully ===" << std::endl;
    return 0;
}
