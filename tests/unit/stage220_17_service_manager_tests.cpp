// ============================================================================
// Stage 220.17 Unit Tests: ServiceManager Daemon Reliability & Crash Restart
// ============================================================================

#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <memory>

#include "kernel/bsd_process.h"
#include "services/service_manager.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::services;

static void test_service_launch_and_port_rendezvous() {
    std::cout << "[Test 1] Service registration, launch-on-demand, and port rendezvous...\n";
    BsdProcessManager proc_mgr;
    TEST_ASSERT(proc_mgr.initialize()); // PID 0
    BsdProcess* init_proc = proc_mgr.create_process("launchd", 0); // PID 1
    TEST_ASSERT(init_proc != nullptr);

    ServiceManager sm(proc_mgr);

    ServiceDefinition def{};
    def.name = "com.apple.windowserver";
    def.executable_path = "/System/Library/CoreServices/WindowServer";
    def.ipc_port_name = "com.apple.windowserver.port";
    def.auto_restart = true;
    def.max_restarts = 3;

    TEST_ASSERT(sm.register_service(def));
    TEST_ASSERT(sm.active_service_count() == 0);
    TEST_ASSERT(sm.resolve_service_port("com.apple.windowserver") == 0);

    // Duplicate registration rejected
    TEST_ASSERT(!sm.register_service(def));

    // Start service with assigned port 501
    TEST_ASSERT(sm.start_service("com.apple.windowserver", 501));
    TEST_ASSERT(sm.active_service_count() == 1);
    TEST_ASSERT(sm.resolve_service_port("com.apple.windowserver") == 501);

    const ServiceInstance* inst = sm.get_service("com.apple.windowserver");
    TEST_ASSERT(inst != nullptr);
    TEST_ASSERT(inst->state == ServiceState::Running);
    TEST_ASSERT(inst->pid > 0);

    // Stop service
    TEST_ASSERT(sm.stop_service("com.apple.windowserver"));
    TEST_ASSERT(sm.active_service_count() == 0);
    TEST_ASSERT(inst->state == ServiceState::Stopped);
    TEST_ASSERT(sm.resolve_service_port("com.apple.windowserver") == 0);

    std::cout << "  Passed launch and port rendezvous tests.\n";
}

static void test_crash_restart_and_max_limits() {
    std::cout << "[Test 2] Crash detection, auto-restart, and max restart bound enforcement...\n";
    BsdProcessManager proc_mgr;
    TEST_ASSERT(proc_mgr.initialize());
    proc_mgr.create_process("launchd", 0); // PID 1

    ServiceManager sm(proc_mgr);

    ServiceDefinition def{};
    def.name = "com.apple.daemon.fickle";
    def.executable_path = "/usr/libexec/fickle";
    def.ipc_port_name = "fickle.port";
    def.auto_restart = true;
    def.max_restarts = 2; // Strict limit: max 2 restarts

    TEST_ASSERT(sm.register_service(def));
    TEST_ASSERT(sm.start_service("com.apple.daemon.fickle", 700));

    const ServiceInstance* inst = sm.get_service("com.apple.daemon.fickle");
    TEST_ASSERT(inst != nullptr);
    TEST_ASSERT(inst->state == ServiceState::Running);
    pid_t pid1 = inst->pid;
    TEST_ASSERT(pid1 > 0);

    // Crash 1: non-zero exit code (e.g. 139 = SIGSEGV)
    sm.on_process_terminated(pid1, 139);
    TEST_ASSERT(inst->restart_count == 1);
    TEST_ASSERT(inst->state == ServiceState::Running);
    pid_t pid2 = inst->pid;
    TEST_ASSERT(pid2 > 0 && pid2 != pid1);
    TEST_ASSERT(inst->ipc_port == 700);

    // Crash 2
    sm.on_process_terminated(pid2, 139);
    TEST_ASSERT(inst->restart_count == 2);
    TEST_ASSERT(inst->state == ServiceState::Running);
    pid_t pid3 = inst->pid;
    TEST_ASSERT(pid3 > 0 && pid3 != pid2);

    // Crash 3: exceeds max_restarts (2) -> Must NOT restart!
    sm.on_process_terminated(pid3, 139);
    TEST_ASSERT(inst->restart_count == 2);
    TEST_ASSERT(inst->state == ServiceState::Crashed);
    TEST_ASSERT(inst->pid == 0);
    TEST_ASSERT(sm.active_service_count() == 0);
    TEST_ASSERT(sm.resolve_service_port("com.apple.daemon.fickle") == 0);

    std::cout << "  Passed crash detection and restart bound tests.\n";
}

static void test_normal_termination_no_restart() {
    std::cout << "[Test 3] Normal exit code 0 does not trigger auto-restart...\n";
    BsdProcessManager proc_mgr;
    TEST_ASSERT(proc_mgr.initialize());
    proc_mgr.create_process("launchd", 0);

    ServiceManager sm(proc_mgr);

    ServiceDefinition def{};
    def.name = "com.apple.task.batch";
    def.executable_path = "/usr/bin/batch";
    def.auto_restart = true;
    def.max_restarts = 5;

    TEST_ASSERT(sm.register_service(def));
    TEST_ASSERT(sm.start_service("com.apple.task.batch", 800));

    const ServiceInstance* inst = sm.get_service("com.apple.task.batch");
    pid_t pid = inst->pid;
    TEST_ASSERT(pid > 0);

    // Normal exit (code 0)
    sm.on_process_terminated(pid, 0);
    TEST_ASSERT(inst->state == ServiceState::Terminated);
    TEST_ASSERT(inst->restart_count == 0); // No restart attempted
    TEST_ASSERT(inst->pid == 0);

    std::cout << "  Passed normal exit non-restart tests.\n";
}

int main() {
    std::cout << "=== Stage 220.17 ServiceManager Reliability Tests ===\n";
    test_service_launch_and_port_rendezvous();
    test_crash_restart_and_max_limits();
    test_normal_termination_no_restart();
    std::cout << "All Stage 220.17 tests passed successfully.\n";
    return 0;
}
