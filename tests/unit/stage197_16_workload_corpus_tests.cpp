// tests/unit/stage197_16_workload_corpus_tests.cpp
// Campaign 197: Acceptance Tests M & N
// Verifies Guest Workload Corpus Execution & Differential Testing Mismatch Reporting

#include <iostream>
#include <vector>
#include <cstdint>
#include <fstream>
#include <string>
#include <sstream>
#include <iomanip>

#include "cpu/cpu.h"
#include "loader/macho_loader.h"
#include "loader/real_artifact_harness.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::loader;

static std::string resolve_artifact_path(const std::string& rel) {
    const std::vector<std::string> candidates = {
        rel,
        "WinIOS/" + rel,
        "../" + rel,
        "../../" + rel,
        "D:/WinISO26/WinIOS/" + rel,
        "D:/WinISO26/" + rel
    };
    for (const auto& c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f.is_open()) return c;
    }
    return rel;
}

static std::vector<std::uint8_t> load_file_bytes(const std::string& path) {
    const std::string resolved = resolve_artifact_path(path);
    std::ifstream file(resolved, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Cannot open artifact: " << path << " (resolved: " << resolved << ")" << std::endl;
        return {};
    }
    const auto sz = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(sz);
    file.read(reinterpret_cast<char*>(bytes.data()), sz);
    return bytes;
}

static bool test_guest_atomics_benchmark_execution() {
    std::cout << "  Testing guest_atomics_benchmark workload execution..." << std::endl;
    const std::string path = "tests/artifacts/bin/guest_atomics_benchmark";
    auto bytes = load_file_bytes(path);
    TEST_ASSERT(!bytes.empty(), "Binary bytes must be non-empty");

    const std::string sha = RealArtifactHarness::compute_sha256(bytes.data(), bytes.size());
    std::cout << "    SHA-256: " << sha << " (size: " << bytes.size() << " bytes)" << std::endl;
    TEST_ASSERT(sha == "31b180f933f45b976c47f3a68dd70bb3445a3b539d92dfa939c5d05e0e1acf16",
                "SHA-256 matches manifest");

    Memory mem(0x400000ULL);
    MachOLoadResult load_res = MachOLoader::load_from_buffer(bytes.data(), bytes.size(), mem, 0x0ULL, 0x400000ULL);
    if (!load_res.is_success()) {
        std::cerr << "    MachO load failed: " << load_res.error_message << " (status: " << static_cast<int>(load_res.status) << ")" << std::endl;
    }
    TEST_ASSERT(load_res.is_success(), "Mach-O loaded successfully");

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(load_res.entry_point);
    cpu.set_stack_pointer(0x80000ULL);

    std::size_t steps = 0;
    while (steps < 2000) {
        if (mem.read_u64(0x40000) != 0) {
            break;
        }
        cpu.step(mem);
        ++steps;
    }

    std::uint64_t sig = mem.read_u64(0x40000);
    std::uint64_t final_val = mem.read_u64(0x40008);
    std::cout << "    Workload completed in " << steps << " instructions. Sig: 0x"
              << std::hex << sig << ", Final Val: 0x" << final_val << std::dec << std::endl;

    TEST_ASSERT(sig == 0x197A10CCULL, "Signature must match 0x197A10CC");
    TEST_ASSERT(final_val == 0x0197C0DEULL, "Final CAS value must match 0x0197C0DE");
    return true;
}

static bool test_guest_smp_atomic_stress_execution() {
    std::cout << "  Testing guest_smp_atomic_stress workload execution..." << std::endl;
    const std::string path = "tests/artifacts/bin/guest_smp_atomic_stress";
    auto bytes = load_file_bytes(path);
    TEST_ASSERT(!bytes.empty(), "Binary bytes must be non-empty");

    const std::string sha = RealArtifactHarness::compute_sha256(bytes.data(), bytes.size());
    std::cout << "    SHA-256: " << sha << " (size: " << bytes.size() << " bytes)" << std::endl;
    TEST_ASSERT(sha == "4066369596f6c63f3de2a48959508baafb75e66320618e6ca9e731c84a4491d4",
                "SHA-256 matches manifest");

    Memory mem(0x400000ULL);
    MachOLoadResult load_res = MachOLoader::load_from_buffer(bytes.data(), bytes.size(), mem, 0x0ULL, 0x400000ULL);
    if (!load_res.is_success()) {
        std::cerr << "    MachO load failed: " << load_res.error_message << " (status: " << static_cast<int>(load_res.status) << ")" << std::endl;
    }
    TEST_ASSERT(load_res.is_success(), "Mach-O loaded successfully");

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(load_res.entry_point);
    cpu.set_stack_pointer(0x80000ULL);

    std::size_t steps = 0;
    while (steps < 2000) {
        if (mem.read_u64(0x40000) != 0) {
            break;
        }
        cpu.step(mem);
        ++steps;
    }

    std::uint64_t sig = mem.read_u64(0x40000);
    std::cout << "    Workload completed in " << steps << " instructions. Sig: 0x"
              << std::hex << sig << std::dec << std::endl;

    TEST_ASSERT(sig == 0x41544F4DULL, "Signature must match 'ATOM' (0x41544F4D)");
    TEST_ASSERT(mem.read_u64(0x20000) == 50ULL, "Shared LDADD counter must equal 50");
    TEST_ASSERT(mem.read_u64(0x20008) == 0x1234ULL, "SWP target must equal 0x1234");
    TEST_ASSERT(mem.read_u64(0x20010) == 0x5678ULL, "CAS target must equal 0x5678");

    return true;
}

static bool test_guest_smp_message_passing_execution() {
    std::cout << "  Testing guest_smp_message_passing workload execution..." << std::endl;
    const std::string path = "tests/artifacts/bin/guest_smp_message_passing";
    auto bytes = load_file_bytes(path);
    TEST_ASSERT(!bytes.empty(), "Binary bytes must be non-empty");

    const std::string sha = RealArtifactHarness::compute_sha256(bytes.data(), bytes.size());
    std::cout << "    SHA-256: " << sha << " (size: " << bytes.size() << " bytes)" << std::endl;
    TEST_ASSERT(sha == "d73ba8b97fecd68f329e12b58c7e0c2d37db439ec046b87f4b8989ed87d972a5",
                "SHA-256 matches manifest");

    Memory mem(0x400000ULL);
    MachOLoadResult load_res = MachOLoader::load_from_buffer(bytes.data(), bytes.size(), mem, 0x0ULL, 0x400000ULL);
    if (!load_res.is_success()) {
        std::cerr << "    MachO load failed: " << load_res.error_message << " (status: " << static_cast<int>(load_res.status) << ")" << std::endl;
    }
    TEST_ASSERT(load_res.is_success(), "Mach-O loaded successfully");

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(load_res.entry_point);
    cpu.set_stack_pointer(0x80000ULL);

    std::size_t steps = 0;
    while (steps < 2000) {
        if (mem.read_u64(0x40000) != 0) {
            break;
        }
        cpu.step(mem);
        ++steps;
    }

    std::uint64_t sig = mem.read_u64(0x40000);
    std::cout << "    Workload completed in " << steps << " instructions. Sig: 0x"
              << std::hex << sig << std::dec << std::endl;

    TEST_ASSERT(sig == 0x4D534750ULL, "Signature must match 'MSGP' (0x4D534750)");
    TEST_ASSERT(mem.read_u64(0x20000) == 1ULL, "Flag must be published as 1");
    TEST_ASSERT(mem.read_u64(0x20008) == 0xDEADBEEFULL, "Data payload must equal 0xDEADBEEF");

    return true;
}

// -----------------------------------------------------------------------------
// Acceptance Test N: Differential Testing Exact Mismatch Diagnostics
// -----------------------------------------------------------------------------
struct DiffMismatch {
    std::string category;
    std::string field_name;
    std::uint64_t expected;
    std::uint64_t actual;
};

static std::vector<DiffMismatch> compare_cpu_states(const CPU& expected, const CPU& actual) {
    std::vector<DiffMismatch> diffs;
    if (expected.get_program_counter() != actual.get_program_counter()) {
        diffs.push_back({"PC", "PC", expected.get_program_counter(), actual.get_program_counter()});
    }
    if (expected.get_stack_pointer() != actual.get_stack_pointer()) {
        diffs.push_back({"SP", "SP", expected.get_stack_pointer(), actual.get_stack_pointer()});
    }
    if (expected.get_nzcv() != actual.get_nzcv()) {
        diffs.push_back({"NZCV", "NZCV", expected.get_nzcv(), actual.get_nzcv()});
    }
    for (std::size_t i = 0; i < 31; ++i) {
        if (expected.read_register(i) != actual.read_register(i)) {
            diffs.push_back({"GPR", "X" + std::to_string(i), expected.read_register(i), actual.read_register(i)});
        }
    }
    return diffs;
}

static bool test_differential_mismatch_diagnostics() {
    std::cout << "  Testing differential testing exact mismatch diagnostics (Acceptance Test N)..." << std::endl;
    CPU ref_cpu;
    CPU test_cpu;

    // Set identical baselines
    for (std::size_t i = 0; i < 31; ++i) {
        ref_cpu.write_register(i, 0x1000 + i);
        test_cpu.write_register(i, 0x1000 + i);
    }
    ref_cpu.set_program_counter(0x80001000ULL);
    test_cpu.set_program_counter(0x80001000ULL);

    // Initial check: zero diffs
    auto initial_diffs = compare_cpu_states(ref_cpu, test_cpu);
    TEST_ASSERT(initial_diffs.empty(), "Zero diffs between identical CPU states");

    // Inject deliberate mismatches:
    // 1. PC mismatch
    test_cpu.set_program_counter(0x80001004ULL);
    // 2. GPR X5 mismatch
    test_cpu.write_register(5, 0xDEADBEEFULL);
    // 3. NZCV mismatch
    test_cpu.set_nzcv(0x8); // N flag set on test_cpu only

    auto diffs = compare_cpu_states(ref_cpu, test_cpu);
    std::cout << "    Detected " << diffs.size() << " exact mismatches:" << std::endl;
    for (const auto& d : diffs) {
        std::cout << "      [" << d.category << "] " << d.field_name
                  << " -> Expected: 0x" << std::hex << d.expected
                  << ", Actual: 0x" << d.actual << std::dec << std::endl;
    }

    TEST_ASSERT(diffs.size() == 3, "Exactly 3 mismatches detected");
    TEST_ASSERT(diffs[0].field_name == "PC", "PC mismatch correctly classified");
    TEST_ASSERT(diffs[1].field_name == "NZCV", "NZCV mismatch correctly classified");
    TEST_ASSERT(diffs[2].field_name == "X5", "X5 mismatch correctly classified");
    TEST_ASSERT(diffs[2].actual == 0xDEADBEEFULL, "Actual X5 value matches injected mutation");

    return true;
}

int main() {
    std::cout << "[STAGE 197.16/17] Running Guest Workload Corpus & Differential Tests (Acceptance Tests M & N)..." << std::endl;

    if (!test_guest_atomics_benchmark_execution()) {
        std::cerr << "test_guest_atomics_benchmark_execution FAILED" << std::endl;
        return 1;
    }

    if (!test_guest_smp_atomic_stress_execution()) {
        std::cerr << "test_guest_smp_atomic_stress_execution FAILED" << std::endl;
        return 1;
    }

    if (!test_guest_smp_message_passing_execution()) {
        std::cerr << "test_guest_smp_message_passing_execution FAILED" << std::endl;
        return 1;
    }

    if (!test_differential_mismatch_diagnostics()) {
        std::cerr << "test_differential_mismatch_diagnostics FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 197.16/17] All Guest Workload and Differential Tests PASSED." << std::endl;
    return 0;
}
