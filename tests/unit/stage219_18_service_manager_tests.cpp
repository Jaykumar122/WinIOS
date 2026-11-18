#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

#include "services/service_manager.h"
#include "kernel/bsd_process.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::services;
using namespace winios::kernel;

void test_service_registration_and_rendezvous() {
    std::cout << "[Test 1] Stage 219.18: Service Registration & Port Rendezvous...\n";

    BsdProcessManager proc_mgr;
    proc_mgr.initialize(); // Creates PID 0

    ServiceManager sm(proc_mgr);

    ServiceDefinition ws_def{
        "com.apple.windowserver",
        "/System/Library/CoreServices/WindowServer",
        "windowserver_port",
        false,
        0
    };

    ServiceDefinition worker_def{
        "com.winios.workerd",
        "/usr/libexec/workerd",
        "workerd_port",
        false,
        0
    };

    TEST_ASSERT(sm.register_service(ws_def));
    TEST_ASSERT(sm.register_service(worker_def));
    // Duplicate registration should fail
    TEST_ASSERT(!sm.register_service(ws_def));

    auto services = sm.list_services();
    TEST_ASSERT(services.size() == 2);
    TEST_ASSERT(services[0] == "com.apple.windowserver");
    TEST_ASSERT(services[1] == "com.winios.workerd");

    // Before start: port resolution returns 0
    TEST_ASSERT(sm.resolve_service_port("com.apple.windowserver") == 0);

    // Start services
    TEST_ASSERT(sm.start_service("com.apple.windowserver", 1001));
    TEST_ASSERT(sm.start_service("com.winios.workerd", 1002));
    TEST_ASSERT(sm.active_service_count() == 2);

    // After start: port resolution returns assigned port
    TEST_ASSERT(sm.resolve_service_port("com.apple.windowserver") == 1001);
    TEST_ASSERT(sm.resolve_service_port("com.winios.workerd") == 1002);
    TEST_ASSERT(sm.resolve_service_port("com.unknown.service") == 0);

    // Stop service
    TEST_ASSERT(sm.stop_service("com.winios.workerd"));
    TEST_ASSERT(sm.active_service_count() == 1);
    TEST_ASSERT(sm.resolve_service_port("com.winios.workerd") == 0);

    std::cout << "  -> Service registration, port rendezvous, and lifecycle verified.\n";
}

void test_service_auto_restart() {
    std::cout << "[Test 2] Stage 219.18: Service Auto-Restart on Abnormal Exit...\n";

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();

    ServiceManager sm(proc_mgr);

    ServiceDefinition daemon_def{
        "com.winios.superdaemon",
        "/usr/sbin/superdaemon",
        "daemon_port",
        true, // auto-restart enabled
        2     // max 2 restarts
    };

    TEST_ASSERT(sm.register_service(daemon_def));
    TEST_ASSERT(sm.start_service("com.winios.superdaemon", 5555));

    const auto* inst = sm.get_service("com.winios.superdaemon");
    TEST_ASSERT(inst && inst->state == ServiceState::Running);
    pid_t pid1 = inst->pid;
    TEST_ASSERT(pid1 > 0);

    // First crash: non-zero exit code 1 -> should auto-restart
    sm.on_process_terminated(pid1, 1);
    TEST_ASSERT(inst->state == ServiceState::Running);
    TEST_ASSERT(inst->restart_count == 1);
    pid_t pid2 = inst->pid;
    TEST_ASSERT(pid2 > 0 && pid2 != pid1);
    TEST_ASSERT(inst->ipc_port == 5555);

    // Second crash -> should auto-restart again (count == 2)
    sm.on_process_terminated(pid2, 1);
    TEST_ASSERT(inst->state == ServiceState::Running);
    TEST_ASSERT(inst->restart_count == 2);
    pid_t pid3 = inst->pid;
    TEST_ASSERT(pid3 > 0 && pid3 != pid2);

    // Third crash -> exceeds max_restarts (2), state becomes Crashed
    sm.on_process_terminated(pid3, 1);
    TEST_ASSERT(inst->state == ServiceState::Crashed);
    TEST_ASSERT(inst->pid == 0);

    std::cout << "  -> Auto-restart recovery and bounded threshold enforcement verified.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 219.18: Minimal Launch-Service Architecture Tests\n";
    std::cout << "=================================================================\n";

    test_service_registration_and_rendezvous();
    test_service_auto_restart();

    std::cout << "\n[PASS] All Stage 219.18 tests passed successfully!\n";
    return 0;
}
