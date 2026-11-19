#include "debugger/application_debugger.h"
#include "debugger/debugger_workflow.h"
#include "platform/app_performance_profiler.h"
#include "kernel/process_identity.h"
#include "loader/app_launcher.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "vfs/vfs.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <cstdlib>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif
#endif

using namespace winios;
using namespace winios::debugger;
using namespace winios::platform;

static void test_debugger_stepping_and_breakpoints_on_cpu() {
    std::cout << "[Test 1] Real CPU stepping, instruction tracing, and breakpoints...\n";

    CPU cpu;
    Memory mem(0x10000); // 64 KB guest RAM

    // Code at 0x4000
    // 0x4000: MOV X0, #42   (0xd2800540)
    // 0x4004: ADD X0, X0, #8 (0x91002000)
    // 0x4008: NOP           (0xd503201f)
    const uint64_t code_base = 0x4000;

    mem.write_u32(code_base + 0, 0xd2800540); // MOV X0, #42
    mem.write_u32(code_base + 4, 0x91002000); // ADD X0, X0, #8
    mem.write_u32(code_base + 8, 0xd503201f); // NOP

    cpu.set_program_counter(code_base);

    auto dbg = std::make_shared<ApplicationDebugger>();
    DebuggerWorkflowController ctrl(nullptr, nullptr, dbg);

    // Attach authorized
    assert(dbg->attach(101, true));
    assert(dbg->is_attached());
    assert(dbg->attached_pid() == 101);

    // Add breakpoint at 0x4008
    ctrl.add_breakpoint(code_base + 8);
    assert(ctrl.has_breakpoint(code_base + 8));

    // Step 1: MOV X0, #42
    auto s1 = ctrl.step(cpu, mem);
    assert(s1 == DebugStopReason::StepCompleted);
    (void)s1;
    assert(cpu.get_program_counter() == code_base + 4);
    assert(cpu.read_register(0) == 42);

    // Step 2: ADD X0, X0, #8 -> next_pc is 0x4008 which has breakpoint
    auto s2 = ctrl.step(cpu, mem);
    assert(s2 == DebugStopReason::BreakpointHit);
    (void)s2;
    assert(cpu.get_program_counter() == code_base + 8);
    assert(cpu.read_register(0) == 50);

    // Step 3 while on breakpoint -> BreakpointHit immediately
    auto s3 = ctrl.step(cpu, mem);
    assert(s3 == DebugStopReason::BreakpointHit);
    (void)s3;
    assert(cpu.get_program_counter() == code_base + 8);

    // Remove breakpoint and step NOP
    ctrl.remove_breakpoint(code_base + 8);
    assert(!ctrl.has_breakpoint(code_base + 8));

    auto s4 = ctrl.step(cpu, mem);
    assert(s4 == DebugStopReason::StepCompleted);
    (void)s4;
    assert(cpu.get_program_counter() == code_base + 12);

    // Verify instruction ring buffer captured 3 executed instructions
    auto crash = ctrl.analyze_crash("TestCheck", cpu);
    assert(crash.recent_instructions.size() == 3);
    assert(crash.recent_instructions[0].pc == code_base);
    assert(crash.recent_instructions[0].opcode == 0xd2800540);
    assert(crash.recent_instructions[1].pc == code_base + 4);
    assert(crash.recent_instructions[1].opcode == 0x91002000);
    assert(crash.recent_instructions[2].pc == code_base + 8);
    assert(crash.recent_instructions[2].opcode == 0xd503201f);

    std::cout << "  -> Passed.\n";
}

static void test_register_snapshot_timing_and_isolation() {
    std::cout << "[Test 2] Register snapshot timing and immutability...\n";

    CPU cpu;
    for (uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0x1000ULL + i);
    }
    cpu.set_stack_pointer(0x7fff'0000ULL);
    cpu.set_program_counter(0x4000ULL);

    ApplicationDebugger dbg;
    RegisterSnapshot snap1 = dbg.capture_registers(cpu);
    assert(snap1.x[0] == 0x1000ULL);
    assert(snap1.x[30] == 0x101eULL);
    assert(snap1.sp == 0x7fff'0000ULL);
    assert(snap1.pc == 0x4000ULL);

    // Modify CPU state
    cpu.write_register(0, 0x9999ULL);
    cpu.set_program_counter(0x5000ULL);

    // Verify snap1 retained exact earlier values (immutable snapshot)
    assert(snap1.x[0] == 0x1000ULL);
    assert(snap1.pc == 0x4000ULL);

    // New snapshot reflects updated CPU state
    RegisterSnapshot snap2 = dbg.capture_registers(cpu);
    assert(snap2.x[0] == 0x9999ULL);
    assert(snap2.pc == 0x5000ULL);

    std::cout << "  -> Passed.\n";
}

static void test_memory_inspection_and_process_isolation() {
    std::cout << "[Test 3] Memory inspection bounds and process authorization...\n";

    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto dbg = std::make_shared<ApplicationDebugger>();
    DebuggerWorkflowController ctrl(nullptr, identity_mgr, dbg);

    // 1. Unauthorized attach rejected
    assert(!dbg->attach(101, false));
    assert(!dbg->is_attached());

    // 2. Attach with invalid handle rejected
    kernel::ProcessHandle bad_handle{999, 1};
    assert(!ctrl.attach_running(bad_handle, "com.winios.bad", true));
    assert(!dbg->is_attached());

    // 3. Allocate valid process in identity manager
    kernel::ApplicationIdentity app{};
    app.bundle_id = "com.winios.test";
    app.display_name = "TestApp";
    kernel::ProcessHandle valid_handle = identity_mgr->allocate_process(app);
    assert(valid_handle.is_valid());

    // Authorized attach to valid handle succeeds
    assert(ctrl.attach_running(valid_handle, "com.winios.test", true));
    assert(dbg->is_attached());
    assert(dbg->attached_pid() == valid_handle.pid);
    assert(ctrl.active_handle().pid == valid_handle.pid);

    // Detach cleans up state
    ctrl.detach();
    assert(!dbg->is_attached());
    assert(dbg->attached_pid() == 0);
    assert(ctrl.active_handle().pid == 0);

    // 4. Memory inspection on unmapped address
    CPU cpu;
    Memory mem(0x1000);
    // PC at unmapped address 0xdead0000 (exceeds 0x1000)
    cpu.set_program_counter(0xdead0000);
    assert(dbg->attach(valid_handle.pid, true));
    auto s_fault = ctrl.step(cpu, mem);
    assert(s_fault == DebugStopReason::Fault);
    (void)s_fault;

    std::cout << "  -> Passed.\n";
}

static void test_profiler_metrics_and_accounting() {
    std::cout << "[Test 4] Profiler measurement units and bounded delta accounting...\n";

    AppPerformanceProfiler profiler;

    // Register process with launch latency in microseconds
    profiler.register_process_launch(102, "com.winios.benchmark", 2500 /* 2500 us */);

    // Record retired instructions (guest emulation instruction counter, not physical PMU)
    profiler.record_process_instructions(102, 10000);
    profiler.record_process_instructions(102, 5000);

    // Record syscalls with ticks
    profiler.record_process_syscall(102, 581, 12);
    profiler.record_process_syscall(102, 582, 18);

    // Memory allocation tracking
    profiler.record_process_memory_delta(102, 131072); // +128 KB
    profiler.record_process_memory_delta(102, 65536);  // +64 KB -> total 192 KB

    auto m1 = profiler.get_process_metrics(102);
    assert(m1.has_value());
    assert(m1->pid == 102);
    assert(m1->bundle_id == "com.winios.benchmark");
    assert(m1->launch_latency_us == 2500);
    assert(m1->instructions_retired == 15000);
    assert(m1->syscalls_dispatched == 2);
    assert(m1->total_syscall_ticks == 30);
    assert(m1->average_syscall_latency_ticks() == 15.0);
    assert(m1->current_memory_bytes == 196608);
    assert(m1->peak_memory_bytes == 196608);
    assert(m1->is_active);

    // Partial deallocation
    profiler.record_process_memory_delta(102, -65536);
    auto m2 = profiler.get_process_metrics(102);
    assert(m2->current_memory_bytes == 131072);
    assert(m2->peak_memory_bytes == 196608); // Peak preserved

    // Excessive deallocation (underflow protection clamps to 0)
    profiler.record_process_memory_delta(102, -500000);
    auto m3 = profiler.get_process_metrics(102);
    assert(m3->current_memory_bytes == 0);
    assert(m3->peak_memory_bytes == 196608);

    // Terminate process
    profiler.mark_process_terminated(102);
    auto m4 = profiler.get_process_metrics(102);
    assert(!m4->is_active);

    // Report generation
    std::string report = profiler.generate_developer_report(102);
    assert(report.find("com.winios.benchmark") != std::string::npos);
    assert(report.find("TERMINATED") != std::string::npos);
    assert(report.find("2500 us") != std::string::npos);
    assert(report.find("15000") != std::string::npos);
    assert(report.find("196608 bytes") != std::string::npos);

    std::cout << "  -> Passed.\n";
}

int main() {
#if defined(_WIN32)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif
    std::cout << "=== Stage 231.19: Debugger and Profiler Correctness Tests ===\n";
    test_debugger_stepping_and_breakpoints_on_cpu();
    test_register_snapshot_timing_and_isolation();
    test_memory_inspection_and_process_isolation();
    test_profiler_metrics_and_accounting();
    std::cout << "=== All Stage 231.19 Tests PASSED ===\n";
    return 0;
}
