#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/launchd_daemon_manager.h"

using namespace winios::runtime;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

bool test_default_daemon_registration() {
    std::cout << "[Test 1] Standard iOS System Launch Daemons Registration...\n";

    LaunchdDaemonManager mgr;
    TEST_ASSERT(mgr.has_job("com.apple.syslogd"), "syslogd must be registered");
    TEST_ASSERT(mgr.has_job("com.apple.notifyd"), "notifyd must be registered");
    TEST_ASSERT(mgr.has_job("com.apple.securityd"), "securityd must be registered");
    TEST_ASSERT(mgr.has_job("com.apple.WindowServer"), "WindowServer must be registered");
    TEST_ASSERT(mgr.has_job("com.apple.SpringBoard"), "SpringBoard must be registered");

    const auto* ws = mgr.get_job("com.apple.WindowServer");
    TEST_ASSERT(ws != nullptr, "WindowServer lookup");
    TEST_ASSERT(ws->state == LaunchdJobState::Idle, "Initial state must be Idle");
    TEST_ASSERT(ws->config.keep_alive, "WindowServer must be KeepAlive");

    std::cout << "  Passed (5 core iOS daemons registered in Idle state)\n";
    return true;
}

bool test_plist_manifest_parser() {
    std::cout << "[Test 2] Property List (.plist) XML Job Parser...\n";

    const char plist_xml[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "    <key>Label</key>\n"
        "    <string>com.apple.mobile.lockdown</string>\n"
        "    <key>Program</key>\n"
        "    <string>/usr/libexec/lockdownd</string>\n"
        "    <key>RunAtLoad</key>\n"
        "    <true/>\n"
        "    <key>KeepAlive</key>\n"
        "    <true/>\n"
        "    <key>MachServices</key>\n"
        "    <dict>\n"
        "        <key>com.apple.mobile.lockdown</key>\n"
        "        <true/>\n"
        "    </dict>\n"
        "</dict>\n"
        "</plist>\n";

    LaunchdDaemonManager mgr;
    bool ok_parse = mgr.load_job_from_plist(plist_xml);
    TEST_ASSERT(ok_parse, "load_job_from_plist must succeed");

    const auto* job = mgr.get_job("com.apple.mobile.lockdown");
    TEST_ASSERT(job != nullptr, "Parsed job lookup");
    TEST_ASSERT(job->config.label == "com.apple.mobile.lockdown", "Label match");
    TEST_ASSERT(!job->config.program_arguments.empty(), "Program args match");
    TEST_ASSERT(job->config.program_arguments[0] == "/usr/libexec/lockdownd", "Program binary match");
    TEST_ASSERT(job->config.run_at_load, "RunAtLoad match");
    TEST_ASSERT(job->config.keep_alive, "KeepAlive match");

    std::cout << "  Passed (Parsed lockdownd plist XML manifest)\n";
    return true;
}

bool test_dependency_chain_resolution() {
    std::cout << "[Test 3] Service Dependency Graph Topology...\n";

    LaunchdDaemonManager mgr;
    const auto* sb = mgr.get_job("com.apple.SpringBoard");
    TEST_ASSERT(sb != nullptr && !sb->config.dependencies.empty(), "SpringBoard must have dependencies");
    TEST_ASSERT(sb->config.dependencies[0] == "com.apple.WindowServer", "SpringBoard depends on WindowServer");

    const auto* ws = mgr.get_job("com.apple.WindowServer");
    TEST_ASSERT(ws != nullptr && !ws->config.dependencies.empty(), "WindowServer must have dependencies");
    TEST_ASSERT(ws->config.dependencies[0] == "com.apple.securityd", "WindowServer depends on securityd");

    const auto* sec = mgr.get_job("com.apple.securityd");
    TEST_ASSERT(sec != nullptr && !sec->config.dependencies.empty(), "securityd must have dependencies");
    TEST_ASSERT(sec->config.dependencies[0] == "com.apple.notifyd", "securityd depends on notifyd");

    const auto* notif = mgr.get_job("com.apple.notifyd");
    TEST_ASSERT(notif != nullptr && !notif->config.dependencies.empty(), "notifyd must have dependencies");
    TEST_ASSERT(notif->config.dependencies[0] == "com.apple.syslogd", "notifyd depends on syslogd");

    std::cout << "  Passed (Validated dependency hierarchy)\n";
    return true;
}

bool test_service_spawning_and_pid_assignment() {
    std::cout << "[Test 4] Service Spawning & Process ID Assignment...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory mem(0x10000);

    bool ok_start = mgr.start_job("com.apple.syslogd", bsd, ipc, mem);
    TEST_ASSERT(ok_start, "start_job must succeed for syslogd");

    const auto* job = mgr.get_job("com.apple.syslogd");
    TEST_ASSERT(job != nullptr, "Job lookup");
    TEST_ASSERT(job->state == LaunchdJobState::Running, "State must be Running");
    TEST_ASSERT(job->pid > 1, "Spawned PID must be > 1");

    auto proc = bsd.get_process(job->pid);
    TEST_ASSERT(proc != nullptr, "BSD process lookup");
    TEST_ASSERT(proc->ppid == 1, "Parent PID must be launchd (1)");

    std::cout << "  Passed (syslogd spawned with PID " << job->pid << ")\n";
    return true;
}

bool test_mach_service_bootstrap_publishing() {
    std::cout << "[Test 5] Mach Service Publishing in Bootstrap Space...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory mem(0x10000);

    mgr.start_job("com.apple.WindowServer", bsd, ipc, mem);

    // Look up WindowServer service via IPC bootstrap registry
    mach_port_t ws_port = ipc.bootstrap_look_up(100, "com.apple.windowserver");
    TEST_ASSERT(ws_port != kMachPortNull, "Bootstrap lookup for WindowServer must succeed");

    mach_port_t hid_port = ipc.bootstrap_look_up(100, "com.apple.iohideventsystem");
    TEST_ASSERT(hid_port != kMachPortNull, "Bootstrap lookup for IOHIDEventSystem must succeed");

    std::cout << "  Passed (Published 'com.apple.windowserver' & 'com.apple.iohideventsystem')\n";
    return true;
}

bool test_hierarchical_boot_orchestration() {
    std::cout << "[Test 6] Full Hierarchical Boot Orchestration...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory mem(0x10000);

    std::size_t count = mgr.launch_system_boot_daemons(bsd, ipc, mem);
    TEST_ASSERT(count == 5, "Must launch all 5 system daemons");

    TEST_ASSERT(mgr.get_job("com.apple.syslogd")->state == LaunchdJobState::Running, "syslogd running");
    TEST_ASSERT(mgr.get_job("com.apple.notifyd")->state == LaunchdJobState::Running, "notifyd running");
    TEST_ASSERT(mgr.get_job("com.apple.securityd")->state == LaunchdJobState::Running, "securityd running");
    TEST_ASSERT(mgr.get_job("com.apple.WindowServer")->state == LaunchdJobState::Running, "WindowServer running");
    TEST_ASSERT(mgr.get_job("com.apple.SpringBoard")->state == LaunchdJobState::Running, "SpringBoard running");

    std::cout << "  Passed (Orchestrated launch of 5 daemons including SpringBoard)\n";
    return true;
}

bool test_service_stopping() {
    std::cout << "[Test 7] Clean Service Termination...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory mem(0x10000);

    mgr.start_job("com.apple.syslogd", bsd, ipc, mem);
    bool ok_stop = mgr.stop_job("com.apple.syslogd", bsd);
    TEST_ASSERT(ok_stop, "stop_job must succeed");

    const auto* job = mgr.get_job("com.apple.syslogd");
    TEST_ASSERT(job->state == LaunchdJobState::Stopped, "State must be Stopped");

    std::cout << "  Passed (Service stopped cleanly)\n";
    return true;
}

bool test_keepalive_restart_on_crash() {
    std::cout << "[Test 8] KeepAlive Automatic Restart on Daemon Crash...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory mem(0x10000);

    mgr.start_job("com.apple.securityd", bsd, ipc, mem);
    const auto* job = mgr.get_job("com.apple.securityd");
    std::uint32_t orig_pid = job->pid;

    // Simulate crash with exit code 1
    mgr.handle_process_exit(orig_pid, 1, bsd, ipc, mem);

    const auto* restarted_job = mgr.get_job("com.apple.securityd");
    TEST_ASSERT(restarted_job->state == LaunchdJobState::Running, "Must restart to Running");
    TEST_ASSERT(restarted_job->crash_count == 1, "Crash count must be 1");
    TEST_ASSERT(restarted_job->pid != orig_pid, "Must have new PID after restart");

    std::cout << "  Passed (securityd restarted with new PID " << restarted_job->pid << ")\n";
    return true;
}

bool test_non_keepalive_clean_exit() {
    std::cout << "[Test 9] Non-KeepAlive One-Shot Job Clean Exit...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory mem(0x10000);

    LaunchdJobConfig oneshot;
    oneshot.label = "com.apple.oneshot";
    oneshot.program_arguments = {"/usr/bin/true"};
    oneshot.run_at_load = false;
    oneshot.keep_alive = false; // One-shot!
    mgr.register_job(std::move(oneshot));

    mgr.start_job("com.apple.oneshot", bsd, ipc, mem);
    const auto* job = mgr.get_job("com.apple.oneshot");
    std::uint32_t pid = job->pid;

    // Exit with code 0
    mgr.handle_process_exit(pid, 0, bsd, ipc, mem);

    const auto* final_job = mgr.get_job("com.apple.oneshot");
    TEST_ASSERT(final_job->state == LaunchdJobState::Stopped, "One-shot must stay Stopped");

    std::cout << "  Passed (One-shot task terminated without restart)\n";
    return true;
}

bool test_full_boot_sequence_verification() {
    std::cout << "[Test 10] Complete iOS Bootstrap to SpringBoard Integration...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory mem(0x10000);

    // Boot system
    std::size_t launched = mgr.launch_system_boot_daemons(bsd, ipc, mem);
    TEST_ASSERT(launched == 5, "5 daemons launched");

    // Verify SpringBoard process credentials and Mach connections
    const auto* sb = mgr.get_job("com.apple.SpringBoard");
    auto sb_proc = bsd.get_process(sb->pid);
    TEST_ASSERT(sb_proc != nullptr, "SpringBoard process lookup");
    TEST_ASSERT(sb_proc->executable_path == "/System/Library/CoreServices/SpringBoard.app/SpringBoard",
                "SpringBoard path verified");

    // SpringBoard looks up WindowServer
    mach_port_t ws = ipc.bootstrap_look_up(sb->pid, "com.apple.windowserver");
    TEST_ASSERT(ws != kMachPortNull, "SpringBoard must hold WindowServer send right");

    std::cout << "  Passed (SpringBoard initialized with WindowServer connection)\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 126: LAUNCHD & iOS DAEMON ARCHITECTURE TESTS      \n";
    std::cout << "  Platform: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e        \n";
    std::cout << "  Target: /sbin/launchd Daemon Supervision & Bootstrap Service   \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_default_daemon_registration()) passed++;
    if (test_plist_manifest_parser()) passed++;
    if (test_dependency_chain_resolution()) passed++;
    if (test_service_spawning_and_pid_assignment()) passed++;
    if (test_mach_service_bootstrap_publishing()) passed++;
    if (test_hierarchical_boot_orchestration()) passed++;
    if (test_service_stopping()) passed++;
    if (test_keepalive_restart_on_crash()) passed++;
    if (test_non_keepalive_clean_exit()) passed++;
    if (test_full_boot_sequence_verification()) passed++;

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "Stage 126 Tests Complete: " << passed << " / " << total << " passed\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
