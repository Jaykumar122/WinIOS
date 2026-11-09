#include <cstdint>
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
            std::cerr << "[ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Default iOS Daemons & Dependency Hierarchy
bool test_default_ios_daemons() {
    std::cout << "[Test 1] Default iOS Daemons & Dependency Hierarchy...\n";

    LaunchdDaemonManager mgr;

    TEST_ASSERT(mgr.has_job("com.apple.syslogd"), "syslogd must be present");
    TEST_ASSERT(mgr.has_job("com.apple.notifyd"), "notifyd must be present");
    TEST_ASSERT(mgr.has_job("com.apple.securityd"), "securityd must be present");
    TEST_ASSERT(mgr.has_job("com.apple.WindowServer"), "WindowServer must be present");
    TEST_ASSERT(mgr.has_job("com.apple.SpringBoard"), "SpringBoard must be present");

    auto syslogd = mgr.get_job("com.apple.syslogd");
    auto notifyd = mgr.get_job("com.apple.notifyd");
    auto securityd = mgr.get_job("com.apple.securityd");
    auto ws = mgr.get_job("com.apple.WindowServer");
    auto sb = mgr.get_job("com.apple.SpringBoard");

    TEST_ASSERT(syslogd != nullptr && syslogd->state == LaunchdJobState::Idle, "syslogd starts Idle");
    TEST_ASSERT(notifyd != nullptr && notifyd->state == LaunchdJobState::Idle, "notifyd starts Idle");
    TEST_ASSERT(securityd != nullptr && securityd->state == LaunchdJobState::Idle, "securityd starts Idle");
    TEST_ASSERT(ws != nullptr && ws->state == LaunchdJobState::Idle, "WindowServer starts Idle");
    TEST_ASSERT(sb != nullptr && sb->state == LaunchdJobState::Idle, "SpringBoard starts Idle");

    // Dependency chain checks
    TEST_ASSERT(notifyd->config.dependencies.size() == 1 && notifyd->config.dependencies[0] == "com.apple.syslogd",
                "notifyd depends on syslogd");
    TEST_ASSERT(securityd->config.dependencies.size() == 1 && securityd->config.dependencies[0] == "com.apple.notifyd",
                "securityd depends on notifyd");
    TEST_ASSERT(ws->config.dependencies.size() == 1 && ws->config.dependencies[0] == "com.apple.securityd",
                "WindowServer depends on securityd");
    TEST_ASSERT(sb->config.dependencies.size() == 1 && sb->config.dependencies[0] == "com.apple.WindowServer",
                "SpringBoard depends on WindowServer");

    return true;
}

// Test 2: Launchd Job Registration and Property List XML Parsing
bool test_plist_parsing_and_custom_job() {
    std::cout << "[Test 2] Launchd Job Registration and Property List XML Parsing...\n";

    LaunchdDaemonManager mgr;

    const std::string plist_data = 
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "    <key>Label</key>\n"
        "    <string>com.apple.testdaemon</string>\n"
        "    <key>ProgramArguments</key>\n"
        "    <array>\n"
        "        <string>/usr/libexec/testdaemon</string>\n"
        "        <string>--arg1</string>\n"
        "    </array>\n"
        "    <key>RunAtLoad</key>\n"
        "    <true/>\n"
        "    <key>KeepAlive</key>\n"
        "    <true/>\n"
        "    <key>MachServices</key>\n"
        "    <dict>\n"
        "        <key>com.apple.testdaemon.service</key>\n"
        "        <true/>\n"
        "    </dict>\n"
        "</dict>\n"
        "</plist>\n";

    bool loaded = mgr.load_job_from_plist(plist_data);
    TEST_ASSERT(loaded, "load_job_from_plist must succeed for valid XML");
    TEST_ASSERT(mgr.has_job("com.apple.testdaemon"), "Custom daemon label registered");

    auto job = mgr.get_job("com.apple.testdaemon");
    TEST_ASSERT(job != nullptr, "Job pointer must be non-null");
    TEST_ASSERT(job->config.run_at_load == true, "run_at_load parsed as true");
    TEST_ASSERT(job->config.keep_alive == true, "keep_alive parsed as true");
    TEST_ASSERT(job->config.program_arguments.size() == 2, "2 program arguments parsed");
    TEST_ASSERT(job->config.program_arguments[0] == "/usr/libexec/testdaemon", "Arg 0 parsed correctly");
    TEST_ASSERT(job->config.mach_services.size() == 1 && job->config.mach_services[0] == "com.apple.testdaemon.service",
                "MachServices parsed correctly");

    return true;
}

// Test 3: Dependency-Ordered System Boot Daemons
bool test_dependency_ordered_system_boot() {
    std::cout << "[Test 3] Dependency-Ordered System Boot Daemons...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory memory(0x100000);

    std::size_t launched = mgr.launch_system_boot_daemons(bsd, ipc, memory);
    TEST_ASSERT(launched == 5, "Must launch all 5 system boot daemons");

    auto syslogd = mgr.get_job("com.apple.syslogd");
    auto notifyd = mgr.get_job("com.apple.notifyd");
    auto securityd = mgr.get_job("com.apple.securityd");
    auto ws = mgr.get_job("com.apple.WindowServer");
    auto sb = mgr.get_job("com.apple.SpringBoard");

    TEST_ASSERT(syslogd->state == LaunchdJobState::Running, "syslogd is Running");
    TEST_ASSERT(notifyd->state == LaunchdJobState::Running, "notifyd is Running");
    TEST_ASSERT(securityd->state == LaunchdJobState::Running, "securityd is Running");
    TEST_ASSERT(ws->state == LaunchdJobState::Running, "WindowServer is Running");
    TEST_ASSERT(sb->state == LaunchdJobState::Running, "SpringBoard is Running");

    // All daemons must have valid assigned PIDs
    TEST_ASSERT(syslogd->pid > 0, "syslogd has positive PID");
    TEST_ASSERT(notifyd->pid > 0, "notifyd has positive PID");
    TEST_ASSERT(securityd->pid > 0, "securityd has positive PID");
    TEST_ASSERT(ws->pid > 0, "WindowServer has positive PID");
    TEST_ASSERT(sb->pid > 0, "SpringBoard has positive PID");

    return true;
}

// Test 4: Mach Bootstrap Service Registration & Port Lookup
bool test_mach_bootstrap_service_registration() {
    std::cout << "[Test 4] Mach Bootstrap Service Registration & Port Lookup...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory memory(0x100000);

    mgr.launch_system_boot_daemons(bsd, ipc, memory);

    const std::uint32_t client_task_id = 100;
    mach_port_t ws_port = ipc.bootstrap_look_up(client_task_id, "com.apple.windowserver");
    TEST_ASSERT(ws_port != kMachPortNull, "Lookup for com.apple.windowserver must succeed");

    mach_port_t sb_port = ipc.bootstrap_look_up(client_task_id, "com.apple.springboard.services");
    TEST_ASSERT(sb_port != kMachPortNull, "Lookup for com.apple.springboard.services must succeed");

    mach_port_t missing_port = ipc.bootstrap_look_up(client_task_id, "com.apple.nonexistent.service");
    TEST_ASSERT(missing_port == kMachPortNull, "Lookup for non-existent service must return kMachPortNull");

    return true;
}

// Test 5: Process Exit & KeepAlive Auto-Restart
bool test_keepalive_restart_on_crash() {
    std::cout << "[Test 5] Process Exit & KeepAlive Auto-Restart...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory memory(0x100000);

    mgr.launch_system_boot_daemons(bsd, ipc, memory);

    auto syslogd = mgr.get_job("com.apple.syslogd");
    TEST_ASSERT(syslogd != nullptr, "syslogd must exist");
    std::uint32_t initial_pid = syslogd->pid;
    TEST_ASSERT(initial_pid > 0, "Initial PID must be valid");

    // Simulate crash (non-zero exit code)
    mgr.handle_process_exit(initial_pid, 139 /* SIGSEGV equivalent */, bsd, ipc, memory);

    auto restarted_syslogd = mgr.get_job("com.apple.syslogd");
    TEST_ASSERT(restarted_syslogd != nullptr, "syslogd exists after restart");
    TEST_ASSERT(restarted_syslogd->crash_count == 1, "Crash count must be incremented to 1");
    TEST_ASSERT(restarted_syslogd->last_exit_code == 139, "Last exit code recorded");
    TEST_ASSERT(restarted_syslogd->state == LaunchdJobState::Running, "Job automatically restarted to Running state");
    TEST_ASSERT(restarted_syslogd->pid != initial_pid, "Restarted job must be assigned a new PID");

    return true;
}

// Test 6: Graceful Job Termination
bool test_graceful_stop_job() {
    std::cout << "[Test 6] Graceful Job Termination...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory memory(0x100000);

    mgr.launch_system_boot_daemons(bsd, ipc, memory);

    bool stopped = mgr.stop_job("com.apple.WindowServer", bsd);
    TEST_ASSERT(stopped, "stop_job on running WindowServer must succeed");

    auto ws = mgr.get_job("com.apple.WindowServer");
    TEST_ASSERT(ws != nullptr, "WindowServer exists");
    TEST_ASSERT(ws->state == LaunchdJobState::Stopped, "State must be Stopped");

    // Stopping an already stopped job should succeed
    stopped = mgr.stop_job("com.apple.WindowServer", bsd);
    TEST_ASSERT(stopped, "stop_job on stopped job should succeed");

    return true;
}

// Test 7: Multi-Client Bootstrap Service Isolation & Routing
bool test_multi_client_bootstrap_isolation() {
    std::cout << "[Test 7] Multi-Client Bootstrap Service Isolation & Routing...\n";

    LaunchdDaemonManager mgr;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    Memory memory(0x100000);

    mgr.launch_system_boot_daemons(bsd, ipc, memory);

    const std::uint32_t client1 = 201;
    const std::uint32_t client2 = 202;

    mach_port_t port1 = ipc.bootstrap_look_up(client1, "com.apple.windowserver");
    mach_port_t port2 = ipc.bootstrap_look_up(client2, "com.apple.windowserver");

    TEST_ASSERT(port1 != kMachPortNull, "Client 1 receives port");
    TEST_ASSERT(port2 != kMachPortNull, "Client 2 receives port");

    auto space1 = ipc.get_task_port_space(client1);
    auto space2 = ipc.get_task_port_space(client2);
    TEST_ASSERT(space1 != nullptr && space2 != nullptr, "Task port spaces exist");

    auto obj1 = space1->lookup_port(port1);
    auto obj2 = space2->lookup_port(port2);
    TEST_ASSERT(obj1 != nullptr && obj2 != nullptr, "Port objects exist");

    // Both local rights must point to the identical shared underlying Mach port object!
    TEST_ASSERT(obj1 == obj2, "Different clients resolve to the same underlying Mach port object");
    TEST_ASSERT(obj1->service_name() == "com.apple.windowserver", "Service name matches");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 166: launchd & Bootstrap Service Conformance\n";
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

    run_test(test_default_ios_daemons, "Default iOS Daemons & Hierarchy");
    run_test(test_plist_parsing_and_custom_job, "Plist XML Parsing & Job Registration");
    run_test(test_dependency_ordered_system_boot, "Dependency-Ordered System Boot");
    run_test(test_mach_bootstrap_service_registration, "Mach Bootstrap Service Registration");
    run_test(test_keepalive_restart_on_crash, "Process Exit & KeepAlive Auto-Restart");
    run_test(test_graceful_stop_job, "Graceful Job Termination");
    run_test(test_multi_client_bootstrap_isolation, "Multi-Client Bootstrap Isolation & Routing");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
