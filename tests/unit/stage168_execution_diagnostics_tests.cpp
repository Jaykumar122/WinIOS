#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "runtime/execution_diagnostics.h"

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

// Test 1: Instruction Step Logging & Register Diff Tracking
bool test_instruction_step_logging() {
    std::cout << "[Test 1] Instruction Step Logging & Register Diff Tracking...\n";

    ExecutionDiagnostics diag;

    std::vector<RegisterDiff> diffs = {
        {0, 0x0000000000000000ULL, 0x000000000000002AULL} // X0 changed from 0 to 42
    };

    diag.log_instruction_step(
        0x100004000ULL,
        0x100004004ULL,
        0xd2800540u, // mov x0, #42
        0,           // EL0
        diffs,
        "mov x0, #42"
    );

    TEST_ASSERT(diag.step_count() == 1, "Exactly 1 step logged");
    TEST_ASSERT(diag.lifecycle().instructions_retired == 1, "Instructions retired incremented to 1");

    const auto* step = diag.get_step(0);
    TEST_ASSERT(step != nullptr, "Step 0 retrieved");
    TEST_ASSERT(step->step_index == 0, "Step index is 0");
    TEST_ASSERT(step->pc_before == 0x100004000ULL, "PC before match");
    TEST_ASSERT(step->pc_after == 0x100004004ULL, "PC after match");
    TEST_ASSERT(step->instruction_raw == 0xd2800540u, "Instruction raw matches");
    TEST_ASSERT(step->current_el == 0, "Current EL is 0");
    TEST_ASSERT(step->disassembly == "mov x0, #42", "Disassembly matches");
    TEST_ASSERT(step->register_diffs.size() == 1, "1 register diff");
    TEST_ASSERT(step->register_diffs[0].reg_index == 0, "Reg index is 0 (X0)");
    TEST_ASSERT(step->register_diffs[0].new_value == 42, "New value is 42");

    return true;
}

// Test 2: Memory Read/Write Operation Logging
bool test_memory_op_logging() {
    std::cout << "[Test 2] Memory Read/Write Operation Logging...\n";

    ExecutionDiagnostics diag;

    diag.log_memory_op(MemoryOpType::Write, 0x7FFF0000ULL, 8, 0x1234567890ABCDEFULL);
    diag.log_memory_op(MemoryOpType::Read, 0x7FFF0000ULL, 4, 0x90ABCDEFULL);

    TEST_ASSERT(diag.memory_op_count() == 2, "2 memory ops logged");
    TEST_ASSERT(diag.lifecycle().memory_writes == 1, "1 write recorded");
    TEST_ASSERT(diag.lifecycle().memory_reads == 1, "1 read recorded");

    const auto* op0 = diag.get_memory_op(0);
    TEST_ASSERT(op0 != nullptr, "Op 0 exists");
    TEST_ASSERT(op0->type == MemoryOpType::Write, "Op 0 is Write");
    TEST_ASSERT(op0->address == 0x7FFF0000ULL, "Op 0 address matches");
    TEST_ASSERT(op0->size == 8, "Op 0 size is 8");
    TEST_ASSERT(op0->value == 0x1234567890ABCDEFULL, "Op 0 value matches");

    const auto* op1 = diag.get_memory_op(1);
    TEST_ASSERT(op1 != nullptr, "Op 1 exists");
    TEST_ASSERT(op1->type == MemoryOpType::Read, "Op 1 is Read");
    TEST_ASSERT(op1->size == 4, "Op 1 size is 4");

    return true;
}

// Test 3: BSD Syscall Dispatch Logging
bool test_bsd_syscall_logging() {
    std::cout << "[Test 3] BSD Syscall Dispatch Logging...\n";

    ExecutionDiagnostics diag;

    std::array<std::uint64_t, 6> args_write = {1, 0x100008000ULL, 13, 0, 0, 0};
    diag.log_syscall(TrapKind::BsdSyscall, 4, "SYS_write", args_write, 13);

    std::array<std::uint64_t, 6> args_exit = {0, 0, 0, 0, 0, 0};
    diag.log_syscall(TrapKind::BsdSyscall, 1, "SYS_exit", args_exit, 0);

    TEST_ASSERT(diag.syscall_count() == 2, "2 syscalls logged");
    TEST_ASSERT(diag.lifecycle().bsd_syscalls_count == 2, "2 BSD syscalls count");
    TEST_ASSERT(diag.lifecycle().mach_traps_count == 0, "0 Mach traps count");

    const auto* call0 = diag.get_syscall(0);
    TEST_ASSERT(call0 != nullptr, "Call 0 exists");
    TEST_ASSERT(call0->kind == TrapKind::BsdSyscall, "Kind is BsdSyscall");
    TEST_ASSERT(call0->number == 4, "Syscall 4");
    TEST_ASSERT(call0->name == "SYS_write", "Name is SYS_write");
    TEST_ASSERT(call0->return_value == 13, "Return value is 13");
    TEST_ASSERT(!call0->is_error, "Not an error");

    return true;
}

// Test 4: Mach Trap Dispatch Logging
bool test_mach_trap_logging() {
    std::cout << "[Test 4] Mach Trap Dispatch Logging...\n";

    ExecutionDiagnostics diag;

    std::array<std::uint64_t, 6> args_reply = {0, 0, 0, 0, 0, 0};
    diag.log_syscall(TrapKind::MachTrap, -26, "mach_reply_port", args_reply, 0x103);

    std::array<std::uint64_t, 6> args_msg = {0x7FFF0000ULL, 1, 64, 0, 0, 0};
    diag.log_syscall(TrapKind::MachTrap, -31, "mach_msg_trap", args_msg, 0);

    TEST_ASSERT(diag.syscall_count() == 2, "2 traps logged");
    TEST_ASSERT(diag.lifecycle().mach_traps_count == 2, "2 Mach traps recorded");
    TEST_ASSERT(diag.lifecycle().bsd_syscalls_count == 0, "0 BSD syscalls recorded");

    const auto* trap0 = diag.get_syscall(0);
    TEST_ASSERT(trap0 != nullptr, "Trap 0 exists");
    TEST_ASSERT(trap0->kind == TrapKind::MachTrap, "Kind is MachTrap");
    TEST_ASSERT(trap0->number == -26, "Trap number -26");
    TEST_ASSERT(trap0->return_value == 0x103, "Port 0x103 returned");

    return true;
}

// Test 5: Process Lifecycle Tracking (Start, Running, Exit Code)
bool test_process_lifecycle_tracking() {
    std::cout << "[Test 5] Process Lifecycle Tracking...\n";

    ExecutionDiagnostics diag;

    diag.record_process_start(42, "/usr/sbin/syslogd");
    TEST_ASSERT(diag.lifecycle().pid == 42, "PID recorded as 42");
    TEST_ASSERT(diag.lifecycle().executable_path == "/usr/sbin/syslogd", "Executable path match");
    TEST_ASSERT(!diag.lifecycle().exited, "Process is running (not exited)");

    diag.record_process_exit(0);
    TEST_ASSERT(diag.lifecycle().exited, "Process marked as exited");
    TEST_ASSERT(diag.lifecycle().exit_code == 0, "Exit code is 0");

    return true;
}

// Test 6: Formatted Human-Readable Trace & Disassembly Report
bool test_trace_report_formatting() {
    std::cout << "[Test 6] Formatted Human-Readable Trace & Disassembly Report...\n";

    ExecutionDiagnostics diag;
    diag.record_process_start(100, "/Applications/MobileSafari.app/MobileSafari");

    std::vector<RegisterDiff> diffs = {{0, 0, 1}};
    diag.log_instruction_step(0x1000ULL, 0x1004ULL, 0xd2800020u, 0, diffs, "mov x0, #1");

    std::string summary = diag.format_summary();
    TEST_ASSERT(summary.find("WinIOS Execution Diagnostics Summary") != std::string::npos, "Summary contains header");
    TEST_ASSERT(summary.find("PID: 100") != std::string::npos, "Summary contains PID");
    TEST_ASSERT(summary.find("/Applications/MobileSafari.app/MobileSafari") != std::string::npos, "Summary contains app");

    std::string trace = diag.format_trace_report(10);
    TEST_ASSERT(trace.find("mov x0, #1") != std::string::npos, "Trace contains disassembly");
    TEST_ASSERT(trace.find("X0=0x1") != std::string::npos, "Trace contains register diff");

    return true;
}

// Test 7: Diagnostics Reset and Multi-Run Isolation
bool test_diagnostics_reset() {
    std::cout << "[Test 7] Diagnostics Reset and Multi-Run Isolation...\n";

    ExecutionDiagnostics diag;
    diag.record_process_start(1, "/sbin/launchd");
    diag.log_instruction_step(0x1000ULL, 0x1004ULL, 0xd503201fu, 0, {}, "nop");
    diag.log_memory_op(MemoryOpType::Read, 0x2000ULL, 4, 0);
    diag.log_syscall(TrapKind::BsdSyscall, 1, "SYS_exit", {}, 0);

    TEST_ASSERT(diag.step_count() == 1, "Step logged");
    TEST_ASSERT(diag.memory_op_count() == 1, "Memory op logged");
    TEST_ASSERT(diag.syscall_count() == 1, "Syscall logged");

    diag.reset();

    TEST_ASSERT(diag.step_count() == 0, "Steps cleared");
    TEST_ASSERT(diag.memory_op_count() == 0, "Memory ops cleared");
    TEST_ASSERT(diag.syscall_count() == 0, "Syscalls cleared");
    TEST_ASSERT(diag.lifecycle().pid == 0, "Lifecycle PID reset to 0");
    TEST_ASSERT(diag.lifecycle().instructions_retired == 0, "Instructions retired reset to 0");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 168: Execution Diagnostics & Runtime Tracing\n";
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

    run_test(test_instruction_step_logging, "Instruction Step & Register Diff Logging");
    run_test(test_memory_op_logging, "Memory Read/Write Operation Logging");
    run_test(test_bsd_syscall_logging, "BSD Syscall Dispatch Logging");
    run_test(test_mach_trap_logging, "Mach Trap Dispatch Logging");
    run_test(test_process_lifecycle_tracking, "Process Lifecycle Tracking");
    run_test(test_trace_report_formatting, "Formatted Trace & Disassembly Report");
    run_test(test_diagnostics_reset, "Diagnostics Reset & Isolation");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
