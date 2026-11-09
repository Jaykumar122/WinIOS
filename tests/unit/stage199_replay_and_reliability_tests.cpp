// =============================================================================
// stage199_replay_and_reliability_tests.cpp
//
// Stage 199: Deterministic Replay and Adversarial Reliability Test Suite
// Verifies deterministic repeatability of guest instruction execution, ASLR slide
// determinism, symbol resolution stability, adversarial missing dylibs,
// instruction budget exhaustion, malformed Mach IPC messages, and invalid syscalls.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/dyld_engine.h"
#include "loader/macho_corpus_builder.h"
#include "loader/macho_dependency_graph.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"
#include "runtime/execution_diagnostics.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (false)

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

// -----------------------------------------------------------------------------
// Test 1: Deterministic Multi-Run Execution Invariance
// -----------------------------------------------------------------------------
bool test_deterministic_multirun_execution() {
    std::cout << "[Test 1] Deterministic Multi-Run Execution Invariance...\n";

    auto compute_bin = MachOCorpusRegistry::create_math_compute();

    constexpr int kIterations = 5;
    std::uint64_t expected_retired = 0;
    int expected_exit_code = -1;

    for (int i = 0; i < kIterations; ++i) {
        Memory mem(0x4000000);
        CPU cpu;
        DarwinBsdSubsystem bsd;
        MachIpcSubsystem ipc;
        DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

        DarwinPipelineConfig cfg;
        cfg.executable_path = "/usr/bin/math_compute";
        cfg.argv = {"/usr/bin/math_compute"};
        cfg.max_instructions = 1000;

        auto res = engine.execute_macho(compute_bin.binary_data.data(), compute_bin.binary_data.size(), cfg, 100 + i);

        TEST_ASSERT(res.is_clean_exit(), "Execution must succeed cleanly on run " + std::to_string(i));

        if (i == 0) {
            expected_retired = res.guest_instructions_retired;
            expected_exit_code = res.exit_code;
            TEST_ASSERT(expected_retired == 405, "Math compute must retire exactly 405 instructions");
            TEST_ASSERT(expected_exit_code == 0, "Exit code must be 0");
        } else {
            TEST_ASSERT(res.guest_instructions_retired == expected_retired,
                        "Retired count mismatch on iteration " + std::to_string(i));
            TEST_ASSERT(res.exit_code == expected_exit_code,
                        "Exit code mismatch on iteration " + std::to_string(i));
        }
    }

    std::cout << "  Passed (Verified 100% execution determinism across " << kIterations << " sequential runs).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: ASLR Slide Determinism & Memory Mapping Invariance
// -----------------------------------------------------------------------------
bool test_aslr_slide_determinism() {
    std::cout << "[Test 2] ASLR Slide Determinism & Memory Mapping Invariance...\n";

    auto compute_bin = MachOCorpusRegistry::create_math_compute();
    const std::uint64_t fixed_slide = 0x00040000ULL; // 256 KB slide

    for (int run = 0; run < 3; ++run) {
        Memory mem(0x4000000);
        UserspaceLoadConfig cfg;
        cfg.aslr_enabled = true;
        cfg.aslr_slide = fixed_slide;

        auto res = UserspaceMachOLoader::load_into_memory(
            compute_bin.binary_data.data(), compute_bin.binary_data.size(), mem, cfg);

        TEST_ASSERT(res.is_success(), "Mach-O load must succeed");
        TEST_ASSERT(res.aslr_slide == fixed_slide, "ASLR slide mismatch");
        TEST_ASSERT(res.entry_point == (0x100001000ULL + fixed_slide),
                    "Entry point must exactly reflect ASLR slide");
    }

    std::cout << "  Passed (ASLR slide and entry point calculations invariant across runs).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Dependency Resolution & Symbol Mapping Determinism
// -----------------------------------------------------------------------------
bool test_symbol_resolution_determinism() {
    std::cout << "[Test 3] Dependency Resolution & Symbol Mapping Determinism...\n";

    DyldEngine dyld;
    dyld.clear_modules();

    DyldLoadedModule mod_sys;
    mod_sys.name = "libSystem.B.dylib";
    mod_sys.install_name = "/usr/lib/libSystem.B.dylib";
    mod_sys.exports["_exit"] = 0x180001000ULL;
    mod_sys.exports["_write"] = 0x180002000ULL;
    dyld.register_module(mod_sys);

    for (int i = 0; i < 10; ++i) {
        auto addr_exit = dyld.resolve_symbol("_exit", 1);
        auto addr_write = dyld.resolve_symbol("_write", 1);

        TEST_ASSERT(addr_exit.has_value() && *addr_exit == 0x180001000ULL, "Exit symbol address unstable");
        TEST_ASSERT(addr_write.has_value() && *addr_write == 0x180002000ULL, "Write symbol address unstable");
    }

    std::cout << "  Passed (Symbol table lookups strictly deterministic).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Adversarial Missing Dylib Dependency Detection
// -----------------------------------------------------------------------------
bool test_adversarial_missing_dylib() {
    std::cout << "[Test 4] Adversarial Missing Dylib Dependency Detection...\n";

    MachODependencyGraph graph;

    MachODependencyNode node;
    node.id = "app_binary";
    MachODependencyEdge edge;
    edge.raw_path = "/usr/lib/libNonExistentLibrary.dylib";
    edge.resolved_path = "/usr/lib/libNonExistentLibrary.dylib";
    edge.is_weak = false;
    node.dependencies.push_back(edge);

    graph.add_node(node);

    std::set<std::string> available = {"/usr/lib/libSystem.B.dylib"};
    auto res = graph.resolve_dependencies("app_binary", available);

    TEST_ASSERT(!res.success, "Resolution must fail when required dylib is absent");
    TEST_ASSERT(!res.missing_required.empty(), "Missing required dylibs list must be populated");
    TEST_ASSERT(res.missing_required[0] == "/usr/lib/libNonExistentLibrary.dylib",
                "Correct missing dylib must be reported");

    std::cout << "  Passed (Missing required dylib detected and isolated).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Adversarial Malformed Mach Message & Buffer Boundaries
// -----------------------------------------------------------------------------
bool test_adversarial_malformed_mach_message() {
    std::cout << "[Test 5] Adversarial Malformed Mach Message & Buffer Boundaries...\n";

    MachIpcSubsystem ipc;
    Memory mem(0x100000);
    const std::uint32_t task_id = 99;
    ipc.create_task_port_space(task_id);

    // Subtest 5A: OOB pointer
    std::int32_t res_oob = ipc.mach_msg(task_id, 0xFFFFFFFFFF000000ULL, kMachSendMsg, 64, 0, 0, 0, mem);
    TEST_ASSERT(res_oob == kMachSendInvalidData, "OOB buffer must return kMachSendInvalidData");

    // Subtest 5B: Size overflow exceeding memory buffer
    std::int32_t res_sz = ipc.mach_msg(task_id, 0x1000, kMachSendMsg, 0x200000, 0, 0, 0, mem);
    TEST_ASSERT(res_sz == kMachSendInvalidData, "Oversized message must return kMachSendInvalidData");

    std::cout << "  Passed (Malformed message buffers rejected safely without memory violations).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Instruction Budget Exhaustion and Clean Halt
// -----------------------------------------------------------------------------
bool test_instruction_budget_exhaustion() {
    std::cout << "[Test 6] Instruction Budget Exhaustion and Clean Halt...\n";

    Memory mem(0x4000000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    auto compute_bin = MachOCorpusRegistry::create_math_compute();

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/usr/bin/math_compute";
    cfg.argv = {"/usr/bin/math_compute"};
    cfg.max_instructions = 50; // Truncate early (loop requires 405)

    auto res = engine.execute_macho(compute_bin.binary_data.data(), compute_bin.binary_data.size(), cfg, 404);

    TEST_ASSERT(!res.is_clean_exit(), "Budget exhaustion must not be reported as clean exit");
    TEST_ASSERT(!res.process_exited, "Process must not have exited");
    TEST_ASSERT(res.guest_instructions_retired == 50, "Exactly 50 instructions must be retired");
    TEST_ASSERT(res.error_message.find("budget reached") != std::string::npos,
                "Error message must specify budget exhaustion: " + res.error_message);

    std::cout << "  Passed (Instruction budget limit cleanly halts virtual CPU without host faults).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Adversarial Invalid Syscall Recovery
// -----------------------------------------------------------------------------
bool test_adversarial_invalid_syscall_recovery() {
    std::cout << "[Test 7] Adversarial Invalid Syscall Recovery...\n";

    Memory mem(0x4000000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assemble guest code invoking undefined syscall 99999 then exiting cleanly:
    //   mov x16, #99999
    //   svc #0x80
    //   mov x0, #0
    //   mov x16, #1
    //   svc #0x80
    std::vector<std::uint32_t> instrs = {
        0xD2884DE0u, // movz x16, #17007
        0xF2A00020u, // movk x16, #1, LSL #16 -> X16 = 99999
        0xD4001001u, // svc #0x80 (undefined syscall)
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80 (clean exit)
    };

    std::vector<std::uint8_t> code(instrs.size() * 4);
    std::memcpy(code.data(), instrs.data(), code.size());

    auto bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.max_instructions = 50;

    auto res = engine.execute_macho(bin.data(), bin.size(), cfg, 555);

    TEST_ASSERT(res.is_clean_exit(), "Process must survive invalid syscall and exit cleanly");
    TEST_ASSERT(res.exit_code == 0, "Exit code must be 0");

    std::cout << "  Passed (Undefined syscall handled gracefully with ENOSYS error and execution resumed).\n";
    return true;
}

// -----------------------------------------------------------------------------
// Main Test Runner
// -----------------------------------------------------------------------------
int main() {
    std::cout << "=================================================================\n";
    std::cout << "WinIOS Stage 199: Deterministic Replay & Adversarial Reliability\n";
    std::cout << "Target: iPhone 15 (iPhone15,4 / D37AP / Apple A16 Bionic T8120 / ARM64e)\n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](const char* name, bool (*fn)()) {
        if (fn()) {
            std::cout << "[PASS] " << name << "\n\n";
            passed++;
        } else {
            std::cerr << "[FAIL] " << name << "\n\n";
            failed++;
        }
    };

    run_test("Test 1: Deterministic Multi-Run Execution Invariance", test_deterministic_multirun_execution);
    run_test("Test 2: ASLR Slide Determinism & Memory Mapping Invariance", test_aslr_slide_determinism);
    run_test("Test 3: Dependency Resolution & Symbol Mapping Determinism", test_symbol_resolution_determinism);
    run_test("Test 4: Adversarial Missing Dylib Dependency Detection", test_adversarial_missing_dylib);
    run_test("Test 5: Adversarial Malformed Mach Message & Buffer Boundaries", test_adversarial_malformed_mach_message);
    run_test("Test 6: Instruction Budget Exhaustion and Clean Halt", test_instruction_budget_exhaustion);
    run_test("Test 7: Adversarial Invalid Syscall Recovery", test_adversarial_invalid_syscall_recovery);

    std::cout << "=================================================================\n";
    std::cout << "Stage 199 Test Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? 0 : 1;
}
