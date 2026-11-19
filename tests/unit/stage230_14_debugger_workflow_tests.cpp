#include "debugger/debugger_workflow.h"
#include "loader/app_launcher.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "kernel/process_identity.h"
#include "settings/task_manager.h"
#include "platform/resource_budget.h"
#include "security/capability_manager.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"
#include "cpu/cpu.h"
#include "memory/memory.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

using namespace winios;

static void setup_test_app(vfs::VirtualFileSystem& vfs, packaging::ApplicationRegistry& reg, const std::string& bundle_id) {
    const std::string app_dir = "/Applications/" + bundle_id + ".app";
    vfs.mkdir("/Applications");
    vfs.mkdir(app_dir);
    vfs.mkdir(app_dir + "/bin");

    const auto& bin = loader::get_embedded_campaign230_app_startup();
    const std::string full_exec = app_dir + "/bin/app";
    vfs.create_file(full_exec);
    vfs.write_file(full_exec, bin.data(), bin.size());

    packaging::AppRegistration r{};
    r.bundle_id = bundle_id;
    r.name = "StartupApp";
    r.display_name = "Startup Application";
    r.version = "1.0.0";
    r.exec_path = "bin/app";
    r.install_dir = app_dir;
    r.min_sdk_version = 0x00020400;
    r.permissions = {"SettingsRead"};
    reg.register_app(r);
}

static void test_launch_under_debugger() {
    std::cout << "[Test 1] Launching application under debugger...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto registry = std::make_shared<packaging::ApplicationRegistry>();
    auto identity_mgr = std::make_shared<kernel::ProcessIdentityManager>(100, 200, 4);
    auto task_mgr = std::make_shared<settings::TaskManager>();
    auto budget_mgr = std::make_shared<platform::ResourceBudgetManager>();
    auto cap_mgr = std::make_shared<security::CapabilityManager>();
    auto diag_svc = std::make_shared<services::DiagnosticsService>();

    const std::string bid = "com.winios.appstartup";
    setup_test_app(*vfs, *registry, bid);

    auto launcher = std::make_shared<loader::AppLaunchPipeline>(
        vfs, registry, identity_mgr, task_mgr, budget_mgr, cap_mgr, diag_svc);
    auto dbg = std::make_shared<debugger::ApplicationDebugger>();

    debugger::DebuggerWorkflowController ctrl(launcher, identity_mgr, dbg);

    auto res = ctrl.launch_under_debugger(bid, 100);
    assert(res.status == loader::LaunchStatus::Success);
    assert(ctrl.debugger()->is_attached());
    assert(ctrl.debugger()->attached_pid() == res.handle.pid);
    assert(ctrl.active_handle().pid == res.handle.pid);

    ctrl.detach();
    assert(!ctrl.debugger()->is_attached());
    assert(ctrl.active_handle().pid == 0);

    launcher->terminate(res.handle);
    std::cout << "  -> Launch under debugger passed.\n";
}

static void test_stepping_and_breakpoints() {
    std::cout << "[Test 2] Debugger step tracing and breakpoints...\n";
    auto dbg = std::make_shared<debugger::ApplicationDebugger>();
    debugger::DebuggerWorkflowController ctrl(nullptr, nullptr, dbg);

    kernel::ProcessHandle h{105, 1};
    assert(dbg->attach(105, true));

    // Add breakpoint at 0x2008
    ctrl.add_breakpoint(0x2008);
    assert(ctrl.has_breakpoint(0x2008));
    assert(!ctrl.has_breakpoint(0x2004));

    // Step 1: 0x2000 -> 0x2004
    auto r1 = ctrl.record_step(0x2000, 0xd503201f, 0x2004);
    assert(r1 == debugger::DebugStopReason::StepCompleted);
    (void)r1;

    // Step 2: 0x2004 -> 0x2008 (next_pc hits breakpoint)
    auto r2 = ctrl.record_step(0x2004, 0xd503201f, 0x2008);
    assert(r2 == debugger::DebugStopReason::BreakpointHit);
    (void)r2;

    // Step 3: current_pc at breakpoint
    auto r3 = ctrl.record_step(0x2008, 0xd503201f, 0x200c);
    assert(r3 == debugger::DebugStopReason::BreakpointHit);
    (void)r3;

    ctrl.remove_breakpoint(0x2008);
    assert(!ctrl.has_breakpoint(0x2008));

    // Now step 0x2008 succeeds
    auto r4 = ctrl.record_step(0x2008, 0xd503201f, 0x200c);
    assert(r4 == debugger::DebugStopReason::StepCompleted);
    (void)r4;

    // Test budget exhaustion
    dbg->set_instruction_budget(2);
    auto r5 = ctrl.record_step(0x200c, 0xd503201f, 0x2010);
    assert(r5 == debugger::DebugStopReason::StepCompleted);
    (void)r5;
    auto r6 = ctrl.record_step(0x2010, 0xd503201f, 0x2014);
    assert(r6 == debugger::DebugStopReason::BudgetExhausted);
    (void)r6;

    std::cout << "  -> Stepping and breakpoints passed.\n";
}

static void test_crash_analysis_and_report() {
    std::cout << "[Test 3] Post-mortem crash report generation...\n";
    auto dbg = std::make_shared<debugger::ApplicationDebugger>();
    debugger::DebuggerWorkflowController ctrl(nullptr, nullptr, dbg);

    assert(dbg->attach(101, true));

    // Record some instructions & syscalls
    dbg->record_instruction(0x1000, 0xd503201f);
    dbg->record_instruction(0x1004, 0x910003fd);
    dbg->record_syscall(592, 0x10000, 0x20000, 0);

    debugger::RegisterSnapshot regs{};
    regs.pc = 0x1008;
    regs.sp = 0x7fff1000;
    regs.pstate = 0x60000000;
    regs.x[0] = 0x1234;
    regs.x[1] = 0x5678;

    auto crash = ctrl.analyze_crash_with_regs("Segmentation fault at 0x1008", regs);
    assert(crash.pid == 0); // Not launched via ctrl directly, but snapshot captured
    assert(crash.fault_reason == "Segmentation fault at 0x1008");
    assert(crash.registers.pc == 0x1008);
    assert(crash.recent_instructions.size() == 2);
    assert(crash.recent_syscalls.size() == 1);

    std::string report = debugger::DebuggerWorkflowController::format_crash_report(crash);
    assert(report.find("WINIOS CRASH POST-MORTEM REPORT") != std::string::npos);
    assert(report.find("Segmentation fault at 0x1008") != std::string::npos);
    assert(report.find("0000000000001008") != std::string::npos);
    assert(report.find("Syscall 592") != std::string::npos);

    std::cout << "  -> Crash report formatted successfully:\n" << report << "\n";
}

int main() {
    std::cout << "=== Stage 230.14: Developer Application Debugger Workflow Tests ===\n";
    test_launch_under_debugger();
    test_stepping_and_breakpoints();
    test_crash_analysis_and_report();
    std::cout << "=== All Stage 230.14 Tests PASSED ===\n";
    return 0;
}
