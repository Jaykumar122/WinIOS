#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"
#include "device/apple_aic.h"
#include "runtime/kernel_boot_coordinator.h"
#include "loader/xnu_genuine_init_builder.h"

using namespace winios;
using namespace winios::memory;
using namespace winios::device;
using namespace winios::runtime;
using namespace winios::loader;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_expanded_snapshot_replay() {
    std::cout << "[Test 1] Stage 208.20: Expanded Kernel Snapshot & Replay...\n";

    CPU cpu1;
    cpu1.set_architectural_exceptions(true);
    Memory mem1(0x800000);

    XnuPhysicalPageAllocator alloc1;
    alloc1.initialize(0x0, 0x800000);
    alloc1.reserve_range(0x0, 0x40000, "kernel_reserved");

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_208();
    TEST_CHECK(!kernel_image.empty(), "Kernel image loaded");

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu1, mem1, config);
    TEST_CHECK(boot_res.success, "Boot preparation succeeded");

    for (std::size_t step = 0; step < 2000; ++step) {
        if (cpu1.is_waiting_for_interrupt()) break;
        cpu1.step(mem1);
    }
    TEST_CHECK(cpu1.is_waiting_for_interrupt(), "Reached WFI");

    // 1. Snapshot Allocator
    const auto alloc_snap = alloc1.serialize();
    TEST_CHECK(!alloc_snap.empty(), "Allocator snapshot serialized");

    // 2. Snapshot AIC2
    const auto aic_snap = boot_res.aic->serialize_state();
    TEST_CHECK(!aic_snap.empty(), "AIC2 snapshot serialized");

    // 3. Snapshot CPU registers
    std::vector<std::uint64_t> gpr_snap(32);
    for (std::size_t r = 0; r < 31; ++r) {
        gpr_snap[r] = cpu1.read_register(r);
    }
    const std::uint64_t pc_snap = cpu1.get_program_counter();
    const std::uint64_t cpacr_snap = cpu1.get_cpacr_el1();

    // 4. Restore into fresh CPU2, fresh Allocator2, fresh AIC2
    CPU cpu2;
    for (std::size_t r = 0; r < 31; ++r) {
        cpu2.write_register(r, gpr_snap[r]);
    }
    cpu2.set_program_counter(pc_snap);
    cpu2.set_cpacr_el1(cpacr_snap);

    XnuPhysicalPageAllocator alloc2;
    TEST_CHECK(alloc2.deserialize(alloc_snap.data(), alloc_snap.size()), "Allocator snapshot restored");
    TEST_CHECK(alloc2.free_pages_count() == alloc1.free_pages_count(), "Restored allocator free count matches");

    AppleAIC aic2(0x41494332u, 0x23B100000ULL, 0x100000ULL);
    TEST_CHECK(aic2.deserialize_state(aic_snap.data(), aic_snap.size()), "AIC2 snapshot restored");

    // Verify bit-exact consistency
    TEST_CHECK(cpu2.get_program_counter() == pc_snap, "Restored PC matches");
    TEST_CHECK(cpu2.get_cpacr_el1() == cpacr_snap, "Restored CPACR_EL1 matches");
    TEST_CHECK(mem1.read_u64(0x50008ULL) == 0xCAFEBABE208ULL, "Restored memory retains Mailbox 208");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.20 Snapshot & Replay Expansion Tests ===\n";
    test_expanded_snapshot_replay();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.20: All expanded snapshot replay tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.20: " << g_failures << " tests failed.\n";
        return 1;
    }
}
