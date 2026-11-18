#include "platform/platform_profiler.h"
#include "debugger/application_debugger.h"
#include "kernel/crash_recovery.h"

#include <cassert>
#include <iostream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;

static void test_profiler_metrics() {
    platform::PlatformProfiler profiler;
    profiler.record_instruction_retirement(1000);
    profiler.record_syscall(581, 20);
    profiler.record_syscall(582, 30);
    profiler.record_memory_allocation(1024 * 1024);
    profiler.record_window_presentation();
    profiler.record_event_dispatch();
    profiler.record_persistence_operation(15);

    auto snap = profiler.take_snapshot();
    TEST_ASSERT(snap.instructions_retired == 1000);
    TEST_ASSERT(snap.syscalls_dispatched == 2);
    TEST_ASSERT(snap.total_syscall_duration_ticks == 50);
    TEST_ASSERT(snap.average_syscall_duration_ticks() == 25.0);
    TEST_ASSERT(snap.memory_allocated_bytes == 1024 * 1024);
    TEST_ASSERT(snap.windows_processed == 1);
    TEST_ASSERT(snap.events_dispatched == 1);
    TEST_ASSERT(snap.persistence_ops == 1);
    TEST_ASSERT(snap.persistence_duration_ticks == 15);

    profiler.record_memory_allocation(-512 * 1024);
    TEST_ASSERT(profiler.take_snapshot().memory_allocated_bytes == 512 * 1024);

    profiler.reset();
    auto reset_snap = profiler.take_snapshot();
    TEST_ASSERT(reset_snap.instructions_retired == 0);
    TEST_ASSERT(reset_snap.syscalls_dispatched == 0);
    std::cout << "[PASS] test_profiler_metrics\n";
}

static void test_debugger_attachment_and_budget() {
    debugger::ApplicationDebugger dbg;

    TEST_ASSERT(!dbg.attach(42, false));
    TEST_ASSERT(!dbg.is_attached());

    TEST_ASSERT(dbg.attach(42, true));
    TEST_ASSERT(dbg.is_attached());
    TEST_ASSERT(dbg.attached_pid() == 42);

    dbg.set_instruction_budget(5);
    TEST_ASSERT(dbg.record_instruction(0x1000, 0xd503201f));
    TEST_ASSERT(dbg.record_instruction(0x1004, 0xd503201f));
    TEST_ASSERT(dbg.record_instruction(0x1008, 0xd503201f));
    TEST_ASSERT(dbg.record_instruction(0x100c, 0xd503201f));
    TEST_ASSERT(!dbg.record_instruction(0x1010, 0xd503201f));
    TEST_ASSERT(dbg.is_budget_exhausted());

    debugger::RegisterSnapshot regs{};
    regs.pc = 0x1010;
    regs.sp = 0x7fff0000;
    auto snap = dbg.capture_crash_snapshot_with_regs(42, "com.test.app", "Budget exhausted", regs);
    TEST_ASSERT(snap.pid == 42);
    TEST_ASSERT(snap.fault_reason == "Budget exhausted");
    TEST_ASSERT(snap.recent_instructions.size() == 5);
    TEST_ASSERT(snap.registers.pc == 0x1010);

    dbg.detach();
    TEST_ASSERT(!dbg.is_attached());
    std::cout << "[PASS] test_debugger_attachment_and_budget\n";
}

static void test_crash_recovery_loop() {
    kernel::CrashRecoveryManager crm;
    const std::string bundle = "com.winios.faultyapp";

    crm.record_termination(50, bundle, kernel::TerminationReason::GuestException, 0x4000, 1000, "SEGV");
    TEST_ASSERT(!crm.is_in_crash_loop(bundle, 2000));
    TEST_ASSERT(crm.should_allow_restart(bundle, 2000));

    crm.record_termination(51, bundle, kernel::TerminationReason::GuestException, 0x4000, 3000, "SEGV");
    TEST_ASSERT(!crm.is_in_crash_loop(bundle, 4000));
    TEST_ASSERT(crm.should_allow_restart(bundle, 4000));

    crm.record_termination(52, bundle, kernel::TerminationReason::GuestException, 0x4000, 5000, "SEGV");
    TEST_ASSERT(crm.is_in_crash_loop(bundle, 6000));
    TEST_ASSERT(!crm.should_allow_restart(bundle, 6000));
    TEST_ASSERT(crm.history_size() == 3);

    TEST_ASSERT(!crm.is_in_crash_loop(bundle, 20000000));
    TEST_ASSERT(crm.should_allow_restart(bundle, 20000000));
    std::cout << "[PASS] test_crash_recovery_loop\n";
}

int main() {
    std::cout << "=== Running Stage 229 Crash Recovery & Debugger Tests ===\n";
    test_profiler_metrics();
    test_debugger_attachment_and_budget();
    test_crash_recovery_loop();
    std::cout << "All Stage 229 Crash Recovery & Debugger Tests Passed!\n";
    return 0;
}
