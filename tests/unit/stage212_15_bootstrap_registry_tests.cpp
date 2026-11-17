#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "kernel/bootstrap_registry.h"

using namespace winios::kernel;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << std::endl; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << std::endl; \
        } \
    } while (0)

void test_service_registration_and_lookup() {
    std::cout << "[Test 1] Named Service Registration and Port Lookup..." << std::endl;

    BootstrapServiceRegistry registry;

    // PID 10 registers "com.apple.system.logger" on port 42
    const int ret1 = registry.check_in("com.apple.system.logger", 42, 10);
    TEST_CHECK(ret1 == kBootstrapSuccess, "Registered 'com.apple.system.logger'");
    TEST_CHECK(registry.is_service_registered("com.apple.system.logger"), "Service is registered");
    TEST_CHECK(registry.is_service_active("com.apple.system.logger"), "Service is active");

    // PID 20 looks up "com.apple.system.logger"
    std::uint32_t port = 0;
    const int ret2 = registry.look_up("com.apple.system.logger", port, 20);
    TEST_CHECK(ret2 == kBootstrapSuccess && port == 42, "Look up returned port 42");

    ServiceRecord info{};
    TEST_CHECK(registry.get_service_info("com.apple.system.logger", info), "Retrieved service info");
    TEST_CHECK(info.owner_pid == 10, "Owner PID is 10");
    TEST_CHECK(info.lookup_count == 1, "Lookup count incremented to 1");

    // Look up non-existent service
    std::uint32_t bad_port = 0;
    const int ret3 = registry.look_up("com.apple.missing", bad_port, 20);
    TEST_CHECK(ret3 == kBootstrapUnknownService && bad_port == 0, "Non-existent service returns kBootstrapUnknownService");
}

void test_duplicate_registration_collision() {
    std::cout << "[Test 2] Duplicate Service Registration Rejection..." << std::endl;

    BootstrapServiceRegistry registry;

    registry.check_in("com.apple.audiod", 101, 15);
    // Another process attempts to register the same active service
    const int ret = registry.check_in("com.apple.audiod", 102, 16);
    TEST_CHECK(ret == kBootstrapNameInUse, "Duplicate registration rejected with kBootstrapNameInUse");

    std::uint32_t resolved_port = 0;
    registry.look_up("com.apple.audiod", resolved_port, 20);
    TEST_CHECK(resolved_port == 101, "Port remains original port 101");
}

void test_privilege_enforcement() {
    std::cout << "[Test 3] System Privilege Enforcement..." << std::endl;

    BootstrapServiceRegistry registry;

    // Normal PID 25 attempts to register privileged service
    const int ret_user = registry.check_in("com.apple.security.daemon", 200, 25, true);
    TEST_CHECK(ret_user == kBootstrapNotPrivileged, "Unprivileged PID rejected from privileged service check-in");

    // Init (PID 1) registers privileged service
    const int ret_init = registry.check_in("com.apple.security.daemon", 200, 1, true);
    TEST_CHECK(ret_init == kBootstrapSuccess, "PID 1 successfully registers privileged service");

    ServiceRecord info{};
    registry.get_service_info("com.apple.security.daemon", info);
    TEST_CHECK(info.system_privileged, "Record marks system_privileged = true");
}

void test_process_termination_and_dead_names() {
    std::cout << "[Test 4] Process Termination and Dead-Name Transition..." << std::endl;

    BootstrapServiceRegistry registry;

    // PID 50 registers a worker service
    registry.check_in("com.apple.worker.service", 500, 50);
    TEST_CHECK(registry.is_service_active("com.apple.worker.service"), "Service active before crash");

    // PID 50 crashes/terminates
    registry.handle_process_termination(50);

    TEST_CHECK(!registry.is_service_active("com.apple.worker.service"), "Service no longer active after crash");

    // Lookup of dead service fails
    std::uint32_t port = 0;
    const int lookup_ret = registry.look_up("com.apple.worker.service", port, 60);
    TEST_CHECK(lookup_ret == kBootstrapUnknownService, "Lookup of dead service returns kBootstrapUnknownService");

    // New owner PID 51 re-registers dead service
    const int re_reg = registry.check_in("com.apple.worker.service", 501, 51);
    TEST_CHECK(re_reg == kBootstrapSuccess, "Re-registration of dead service succeeds");

    registry.look_up("com.apple.worker.service", port, 60);
    TEST_CHECK(port == 501, "Resolved port updated to new owner port 501");
}

void test_unregistration_lifecycle() {
    std::cout << "[Test 5] Service Unregistration Lifecycle..." << std::endl;

    BootstrapServiceRegistry registry;

    registry.check_in("com.apple.temp.service", 800, 70);

    // Non-owner PID 71 attempts to unregister
    const int bad_unreg = registry.unregister("com.apple.temp.service", 71);
    TEST_CHECK(bad_unreg == kBootstrapNotPrivileged, "Non-owner cannot unregister service");

    // Owner PID 70 unregisters
    const int good_unreg = registry.unregister("com.apple.temp.service", 70);
    TEST_CHECK(good_unreg == kBootstrapSuccess, "Owner successfully unregisters service");

    TEST_CHECK(!registry.is_service_registered("com.apple.temp.service"), "Service completely removed");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.15 Bootstrap Service Registry Tests ===" << std::endl;
    test_service_registration_and_lookup();
    test_duplicate_registration_collision();
    test_privilege_enforcement();
    test_process_termination_and_dead_names();
    test_unregistration_lifecycle();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.15: All bootstrap service registry tests passed." << std::endl;
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.15: " << g_failures << " tests failed." << std::endl;
        return 1;
    }
}
