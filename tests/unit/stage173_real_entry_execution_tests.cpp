#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/macho_corpus_builder.h"
#include "loader/real_artifact_harness.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"
#include "runtime/execution_diagnostics.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Hello World Mach-O Entry-Point Execution
bool test_hello_world_entry_execution() {
    std::cout << "[Test 1] Hello World Mach-O Entry-Point Execution...\n";

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    auto entry = MachOCorpusRegistry::create_hello_world();
    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/hello";

    auto result = engine.execute_macho(entry.binary_data.data(), entry.binary_data.size(), config, 101);

    TEST_ASSERT(result.process_exited, "Process must have exited");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0");
    TEST_ASSERT(result.is_clean_exit(), "Execution must be clean exit");
    TEST_ASSERT(result.guest_instructions_retired >= 8, "Must retire at least 8 guest instructions");
    TEST_ASSERT(result.stdout_output.find("Hello, Darwin!") != std::string::npos,
                "Stdout must capture \"Hello, Darwin!\"");
    TEST_ASSERT(result.bsd_syscalls_handled >= 2, "Must handle SYS_write and SYS_exit");

    return true;
}

// Test 2: Iterative Math Compute Execution & Register Evolution
bool test_math_compute_entry_execution() {
    std::cout << "[Test 2] Iterative Math Compute Execution & Register Evolution...\n";

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    auto entry = MachOCorpusRegistry::create_math_compute();
    DarwinPipelineConfig config;
    config.max_instructions = 10000;
    config.executable_path = "/usr/bin/math";

    auto result = engine.execute_macho(entry.binary_data.data(), entry.binary_data.size(), config, 102);

    TEST_ASSERT(result.process_exited, "Math program must exit cleanly");
    TEST_ASSERT(result.exit_code == 0, "Exit code 0");
    // 100 loop iterations * 4 instructions/iteration + setup/exit instructions ~ 406 instructions
    TEST_ASSERT(result.guest_instructions_retired > 300, "Must retire loop instructions (> 300)");
    TEST_ASSERT(result.bsd_syscalls_handled == 1, "Handled SYS_exit");

    return true;
}

// Test 3: File I/O Lifecycle (Open, Write, Close, Exit)
bool test_file_io_entry_execution() {
    std::cout << "[Test 3] File I/O Lifecycle Execution...\n";

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    auto entry = MachOCorpusRegistry::create_file_io();
    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/file_io";

    auto result = engine.execute_macho(entry.binary_data.data(), entry.binary_data.size(), config, 103);

    TEST_ASSERT(result.process_exited, "File I/O process must exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code 0");
    TEST_ASSERT(result.bsd_syscalls_handled >= 2, "Handled open/close/exit syscalls");

    return true;
}

// Test 4: Virtual Memory Allocation Lifecycle (mmap + exit)
bool test_memory_mmap_entry_execution() {
    std::cout << "[Test 4] Virtual Memory Allocation Lifecycle Execution...\n";

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    auto entry = MachOCorpusRegistry::create_memory_mmap();
    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/mmap_test";

    auto result = engine.execute_macho(entry.binary_data.data(), entry.binary_data.size(), config, 104);

    TEST_ASSERT(result.process_exited, "Memory allocation process must exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code 0");
    TEST_ASSERT(result.bsd_syscalls_handled >= 2, "Handled mmap and exit syscalls");

    return true;
}

// Test 5: Mach IPC Client Trap Execution (mach_reply_port_trap -26)
bool test_mach_ipc_trap_entry_execution() {
    std::cout << "[Test 5] Mach IPC Trap Execution...\n";

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    auto entry = MachOCorpusRegistry::create_mach_ipc_client();
    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/ipc_client";

    auto result = engine.execute_macho(entry.binary_data.data(), entry.binary_data.size(), config, 105);

    TEST_ASSERT(result.process_exited, "Mach IPC process must exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code 0");
    TEST_ASSERT(result.mach_traps_handled == 1, "Handled 1 Mach trap (-26)");

    return true;
}

// Test 6: Instruction Retirement Budget Exhaustion Protection
bool test_instruction_budget_exhaustion() {
    std::cout << "[Test 6] Instruction Retirement Budget Exhaustion Protection...\n";

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    // Math compute takes ~406 instructions. Set budget to 25.
    auto entry = MachOCorpusRegistry::create_math_compute();
    DarwinPipelineConfig config;
    config.max_instructions = 25;
    config.executable_path = "/usr/bin/math_limited";

    auto result = engine.execute_macho(entry.binary_data.data(), entry.binary_data.size(), config, 106);

    TEST_ASSERT(!result.process_exited, "Must NOT have exited before budget");
    TEST_ASSERT(result.guest_instructions_retired == 25, "Must halt exactly at budget (25 instructions)");
    TEST_ASSERT(result.error_message.find("budget reached") != std::string::npos,
                "Error message indicates instruction budget exhaustion");

    return true;
}

// Test 7: Diagnostics Recording During Execution
bool test_diagnostics_integration() {
    std::cout << "[Test 7] Diagnostics Recording During Execution...\n";

    ExecutionDiagnostics diag;
    diag.record_process_start(200, "/usr/bin/hello");

    // Log retired instruction steps
    std::vector<RegisterDiff> d1 = {{1, 0, 0x1020}};
    diag.log_instruction_step(0x1000, 0x1004, 0xD2820401u, 0, d1, "movz x1, #0x1020");

    std::vector<RegisterDiff> d2 = {{0, 0, 1}};
    diag.log_instruction_step(0x1004, 0x1008, 0xD2800020u, 0, d2, "mov x0, #1");

    diag.log_syscall(TrapKind::BsdSyscall, 4, "SYS_write", {1, 0x1020, 16, 0, 0, 0}, 16);
    diag.record_process_exit(0);

    TEST_ASSERT(diag.step_count() == 2, "2 steps logged");
    TEST_ASSERT(diag.syscall_count() == 1, "1 syscall logged");
    TEST_ASSERT(diag.lifecycle().exited, "Exited status recorded");
    TEST_ASSERT(diag.lifecycle().exit_code == 0, "Exit code 0 recorded");

    std::string report = diag.format_trace_report(10);
    TEST_ASSERT(report.find("movz x1, #0x1020") != std::string::npos, "Report contains instruction 1");
    TEST_ASSERT(report.find("mov x0, #1") != std::string::npos, "Report contains instruction 2");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 173: ARM64 Mach-O Entry-Point Execution\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_hello_world_entry_execution, "Hello World Mach-O Entry Execution");
    run_test(test_math_compute_entry_execution, "Math Compute & Register Evolution");
    run_test(test_file_io_entry_execution, "File I/O Lifecycle Execution");
    run_test(test_memory_mmap_entry_execution, "Memory Allocator Execution");
    run_test(test_mach_ipc_trap_entry_execution, "Mach IPC Trap Execution");
    run_test(test_instruction_budget_exhaustion, "Instruction Budget Exhaustion Protection");
    run_test(test_diagnostics_integration, "Diagnostics Recording Integration");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
