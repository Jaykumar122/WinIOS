#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/launchd_daemon_manager.h"

using namespace winios;
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

// Test 1: Default iOS System Launch Daemons Catalog
bool test_default_daemons_catalog() {
    std::cout << "[Test 1] Default iOS System Launch Daemons Catalog...\n";

    LaunchdDaemonManager mgr;
    TEST_ASSERT(mgr.has_job("com.apple.syslogd"), "syslogd must be registered");
    TEST_ASSERT(mgr.has_job("com.apple.notifyd"), "notifyd must be registered");
    TEST_ASSERT(mgr.has_job("com.apple.securityd"), "securityd must be registered");
    TEST_ASSERT(mgr.has_job("com.apple.WindowServer"), "WindowServer must be registered");
    TEST_ASSERT(mgr.has_job("com.apple.SpringBoard"), "SpringBoard must be registered");

    auto labels = mgr.get_all_labels();
    TEST_ASSERT(labels.size() >= 5, "At least 5 core system daemons registered");

    std::cout << "  -> PASS: Default iOS Launch Daemons Catalog Verified\n";
    return true;
}

// Test 2: Plist XML Configuration Parsing
bool test_plist_configuration_parsing() {
    std::cout << "[Test 2] Property List XML Parsing for Custom Daemons...\n";

    LaunchdDaemonManager mgr;
    const std::string plist = 
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "    <key>Label</key>\n"
        "    <string>com.apple.testdaemon</string>\n"
        "    <key>Program</key>\n"
        "    <string>/usr/libexec/testdaemon</string>\n"
        "    <key>RunAtLoad</key>\n"
        "    <true/>\n"
        "    <key>KeepAlive</key>\n"
        "    <true/>\n"
        "    <key>MachServices</key>\n"
        "    <dict>\n"
        "        <key>com.apple.testservice</key>\n"
        "        <true/>\n"
        "    </dict>\n"
        "</dict>\n"
        "</plist>\n";

    bool loaded = mgr.load_job_from_plist(plist);
    TEST_ASSERT(loaded, "load_job_from_plist must succeed");
    TEST_ASSERT(mgr.has_job("com.apple.testdaemon"), "Custom daemon registered");

    const auto* job = mgr.get_job("com.apple.testdaemon");
    TEST_ASSERT(job != nullptr, "Job pointer non-null");
    TEST_ASSERT(job->config.run_at_load, "RunAtLoad parsed correctly");
    TEST_ASSERT(job->config.keep_alive, "KeepAlive parsed correctly");

    std::cout << "  -> PASS: Plist XML Configuration Parsing Verified\n";
    return true;
}

// Test 3: Topological Dependency Order in Service Startup
bool test_dependency_order_startup() {
    std::cout << "[Test 3] Topological Dependency Order in Service Startup...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory memory(16 * 1024 * 1024);

    // Triggering SpringBoard start must recursively start dependencies:
    // syslogd -> notifyd -> securityd -> WindowServer -> SpringBoard
    bool started = mgr.start_job("com.apple.SpringBoard", bsd, ipc, memory);
    TEST_ASSERT(started, "Starting SpringBoard must succeed");

    const auto* syslogd = mgr.get_job("com.apple.syslogd");
    const auto* notifyd = mgr.get_job("com.apple.notifyd");
    const auto* securityd = mgr.get_job("com.apple.securityd");
    const auto* ws = mgr.get_job("com.apple.WindowServer");
    const auto* sb = mgr.get_job("com.apple.SpringBoard");

    TEST_ASSERT(syslogd && syslogd->state == LaunchdJobState::Running, "syslogd must be running");
    TEST_ASSERT(notifyd && notifyd->state == LaunchdJobState::Running, "notifyd must be running");
    TEST_ASSERT(securityd && securityd->state == LaunchdJobState::Running, "securityd must be running");
    TEST_ASSERT(ws && ws->state == LaunchdJobState::Running, "WindowServer must be running");
    TEST_ASSERT(sb && sb->state == LaunchdJobState::Running, "SpringBoard must be running");

    // Verify process hierarchy: each daemon must be a child of PID 1 (ppid == 1)
    auto p_syslogd = bsd.get_process(syslogd->pid);
    auto p_ws = bsd.get_process(ws->pid);
    auto p_sb = bsd.get_process(sb->pid);

    TEST_ASSERT(p_syslogd && p_syslogd->ppid == 1, "syslogd parent PID must be 1 (launchd)");
    TEST_ASSERT(p_ws && p_ws->ppid == 1, "WindowServer parent PID must be 1 (launchd)");
    TEST_ASSERT(p_sb && p_sb->ppid == 1, "SpringBoard parent PID must be 1 (launchd)");

    std::cout << "  -> PASS: Topological Dependency Order Startup Verified\n";
    return true;
}

// Test 4: Mach Bootstrap Port Registration
bool test_mach_bootstrap_registration() {
    std::cout << "[Test 4] Mach Service Bootstrap Port Registration...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory memory(16 * 1024 * 1024);

    mgr.start_job("com.apple.WindowServer", bsd, ipc, memory);

    // WindowServer advertises "com.apple.windowserver" and "com.apple.iohideventsystem"
    mach_port_t ws_port = ipc.bootstrap_look_up(1, "com.apple.windowserver");
    TEST_ASSERT(ws_port != kMachPortNull, "Must resolve com.apple.windowserver via bootstrap");

    mach_port_t hid_port = ipc.bootstrap_look_up(1, "com.apple.iohideventsystem");
    TEST_ASSERT(hid_port != kMachPortNull, "Must resolve com.apple.iohideventsystem via bootstrap");

    std::cout << "  -> PASS: Mach Bootstrap Port Registration Verified\n";
    return true;
}

// Test 5: Crash Recovery and KeepAlive Restart
bool test_crash_recovery_and_keepalive() {
    std::cout << "[Test 5] Crash Recovery & KeepAlive Automatic Restart...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory memory(16 * 1024 * 1024);

    mgr.start_job("com.apple.notifyd", bsd, ipc, memory);
    const auto* job_pre = mgr.get_job("com.apple.notifyd");
    TEST_ASSERT(job_pre != nullptr && job_pre->state == LaunchdJobState::Running, "notifyd running");
    std::uint32_t original_pid = job_pre->pid;

    // Simulate crash with exit code 139 (SIGSEGV)
    mgr.handle_process_exit(original_pid, 139, bsd, ipc, memory);

    const auto* job_post = mgr.get_job("com.apple.notifyd");
    TEST_ASSERT(job_post != nullptr, "Job pointer non-null");
    TEST_ASSERT(job_post->crash_count == 1, "Crash count incremented to 1");
    TEST_ASSERT(job_post->last_exit_code == 139, "Last exit code recorded as 139");
    TEST_ASSERT(job_post->state == LaunchdJobState::Running, "Service restarted to Running under KeepAlive");
    TEST_ASSERT(job_post->pid > original_pid, "Restarted service receives new distinct PID");

    std::cout << "  -> PASS: Crash Recovery & KeepAlive Restart Verified\n";
    return true;
}

// Test 6: Clean Process Exit without KeepAlive
bool test_clean_exit_without_keepalive() {
    std::cout << "[Test 6] Clean Process Exit without KeepAlive...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory memory(16 * 1024 * 1024);

    LaunchdJobConfig one_shot;
    one_shot.label = "com.apple.oneshot";
    one_shot.program_arguments = {"/usr/bin/oneshot_task"};
    one_shot.run_at_load = false;
    one_shot.keep_alive = false; // No KeepAlive
    mgr.register_job(std::move(one_shot));

    mgr.start_job("com.apple.oneshot", bsd, ipc, memory);
    const auto* job = mgr.get_job("com.apple.oneshot");
    TEST_ASSERT(job && job->state == LaunchdJobState::Running, "Job started");
    std::uint32_t task_pid = job->pid;

    // Process exits cleanly with status 0
    mgr.handle_process_exit(task_pid, 0, bsd, ipc, memory);

    const auto* job_after = mgr.get_job("com.apple.oneshot");
    TEST_ASSERT(job_after->state == LaunchdJobState::Stopped, "One-shot job must transition to Stopped");
    TEST_ASSERT(job_after->crash_count == 0, "No crash recorded for clean exit");

    std::cout << "  -> PASS: Clean Exit without KeepAlive Verified\n";
    return true;
}

// Test 7: Clean Service Shutdown (stop_job)
bool test_service_shutdown() {
    std::cout << "[Test 7] Clean Service Shutdown (SIGTERM Delivery)...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory memory(16 * 1024 * 1024);

    mgr.start_job("com.apple.syslogd", bsd, ipc, memory);
    const auto* job = mgr.get_job("com.apple.syslogd");
    TEST_ASSERT(job && job->state == LaunchdJobState::Running, "syslogd active");
    std::uint32_t pid = job->pid;

    bool stopped = mgr.stop_job("com.apple.syslogd", bsd);
    TEST_ASSERT(stopped, "stop_job must return true");

    const auto* job_stopped = mgr.get_job("com.apple.syslogd");
    TEST_ASSERT(job_stopped->state == LaunchdJobState::Stopped, "Job state must be Stopped");

    // Process must have received SIGTERM (signal 15)
    TEST_ASSERT(bsd.has_pending_signal(pid, 15), "syslogd process must have pending SIGTERM (15)");

    std::cout << "  -> PASS: Clean Service Shutdown Verified\n";
    return true;
}

// Test 8: Full System Boot Daemons Orchestration
bool test_boot_daemons_orchestration() {
    std::cout << "[Test 8] System Boot Daemons Orchestration...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory memory(16 * 1024 * 1024);

    std::size_t launched = mgr.launch_system_boot_daemons(bsd, ipc, memory);
    TEST_ASSERT(launched >= 5, "All default system daemons launched during boot");

    // Verify all core daemons running
    for (const auto& label : {"com.apple.syslogd", "com.apple.notifyd", "com.apple.securityd", "com.apple.WindowServer", "com.apple.SpringBoard"}) {
        const auto* j = mgr.get_job(label);
        TEST_ASSERT(j && j->state == LaunchdJobState::Running, "Daemon must be running after boot orchestration");
    }

    std::cout << "  -> PASS: System Boot Daemons Orchestration Verified\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 152: RUNTIME LIFECYCLE & SERVICE SUPERVISION      \n";
    std::cout << "  Target: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e          \n";
    std::cout << "  Subsystem: PID 1 launchd, Service Dependencies & Recovery      \n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int total = 8;

    if (test_default_daemons_catalog()) passed++;
    if (test_plist_configuration_parsing()) passed++;
    if (test_dependency_order_startup()) passed++;
    if (test_mach_bootstrap_registration()) passed++;
    if (test_crash_recovery_and_keepalive()) passed++;
    if (test_clean_exit_without_keepalive()) passed++;
    if (test_service_shutdown()) passed++;
    if (test_boot_daemons_orchestration()) passed++;

    std::cout << "\n=================================================================\n";
    std::cout << "STAGE 152 RESULTS: " << passed << " PASSED, " << (total - passed) << " FAILED\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
