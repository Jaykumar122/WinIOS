#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "security/capability_manager.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::security;

void test_capability_manager_grants_and_checks() {
    std::cout << "[Test 1] Capability Grants and Verification...\n";
    CapabilityManager cm;

    const uint32_t pid_user = 100;
    // Initially unprivileged process has no explicit capabilities
    TEST_ASSERT(!cm.check(pid_user, Capability::DisplayWindow));
    TEST_ASSERT(!cm.check(pid_user, Capability::ProcessSpawn));

    // Grant DisplayWindow and LoggingAccess
    cm.grant(pid_user, Capability::DisplayWindow);
    cm.grant(pid_user, Capability::LoggingAccess);

    TEST_ASSERT(cm.check(pid_user, Capability::DisplayWindow));
    TEST_ASSERT(cm.check(pid_user, Capability::LoggingAccess));
    TEST_ASSERT(!cm.check(pid_user, Capability::ProcessSpawn));
    TEST_ASSERT(!cm.check(pid_user, Capability::StorageShared));

    std::cout << "[PASS] Grants and checks verified.\n";
}

void test_pid1_superuser_bypass() {
    std::cout << "[Test 2] PID 1 Superuser Capability Checks...\n";
    CapabilityManager cm;

    // PID 1 (Launchd / System) has implicit full permissions
    TEST_ASSERT(cm.check(1, Capability::DisplayWindow));
    TEST_ASSERT(cm.check(1, Capability::ProcessSpawn));
    TEST_ASSERT(cm.check(1, Capability::StoragePrivate));
    TEST_ASSERT(cm.check(1, Capability::StorageShared));
    TEST_ASSERT(cm.check(1, Capability::LoggingAccess));
    TEST_ASSERT(cm.check(1, Capability::ResourceAccess));

    std::cout << "[PASS] PID 1 superuser capabilities verified.\n";
}

void test_capability_named_parsing() {
    std::cout << "[Test 3] Named Permission Strings Parsing...\n";
    CapabilityManager cm;

    const uint32_t pid_app = 200;
    std::vector<std::string> perms = {
        "display.window",
        "storage.private",
        "logging",
        "process.spawn"
    };

    cm.grant_named(pid_app, perms);

    TEST_ASSERT(cm.check(pid_app, Capability::DisplayWindow));
    TEST_ASSERT(cm.check(pid_app, Capability::StoragePrivate));
    TEST_ASSERT(cm.check(pid_app, Capability::LoggingAccess));
    TEST_ASSERT(cm.check(pid_app, Capability::ProcessSpawn));

    // Not granted
    TEST_ASSERT(!cm.check(pid_app, Capability::ClipboardRead));
    TEST_ASSERT(!cm.check(pid_app, Capability::StorageShared));

    // Revocation
    cm.revoke_all(pid_app);
    TEST_ASSERT(!cm.check(pid_app, Capability::DisplayWindow));
    TEST_ASSERT(!cm.check(pid_app, Capability::LoggingAccess));

    std::cout << "[PASS] Named permission parsing and revocation verified.\n";
}

int main() {
    std::cout << "=== WinIOS Stage 221.10: Application Sandbox & Capability Model Tests ===\n";
    test_capability_manager_grants_and_checks();
    test_pid1_superuser_bypass();
    test_capability_named_parsing();
    std::cout << "[ALL PASS] Stage 221.10 tests passed successfully.\n";
    return 0;
}
