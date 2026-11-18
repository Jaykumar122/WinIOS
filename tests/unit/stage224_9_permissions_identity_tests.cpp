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

void test_identity_binding_and_tokens() {
    std::cout << "[Test 1] Process Identity Binding & Security Tokens...\n";

    CapabilityManager cm;
    cm.bind_identity(100, "com.winios.settings", 501);

    auto id_opt = cm.get_identity(100);
    TEST_ASSERT(id_opt.has_value());
    TEST_ASSERT(id_opt->pid == 100);
    TEST_ASSERT(id_opt->bundle_id == "com.winios.settings");
    TEST_ASSERT(id_opt->uid == 501);
    TEST_ASSERT(id_opt->security_token > 0);

    TEST_ASSERT(cm.get_bundle_id(100) == "com.winios.settings");

    // Unbound PID returns nullopt
    TEST_ASSERT(!cm.get_identity(999).has_value());

    cm.unbind_identity(100);
    TEST_ASSERT(!cm.get_identity(100).has_value());

    std::cout << "[PASS] Identity binding verified.\n";
}

void test_capability_enforcement_and_audit() {
    std::cout << "[Test 2] Capability Enforcement & Security Audit Metrics...\n";

    CapabilityManager cm;
    cm.bind_identity(200, "com.winios.demo");

    // Initially no capabilities granted
    TEST_ASSERT(!cm.check(200, Capability::DisplayWindow));
    TEST_ASSERT(!cm.check(200, Capability::ConfigAccess));

    // Grant named permissions
    cm.grant_named(200, { "display.window", "config", "logging" });
    TEST_ASSERT(cm.check(200, Capability::DisplayWindow));
    TEST_ASSERT(cm.check(200, Capability::ConfigAccess));
    TEST_ASSERT(cm.check(200, Capability::LoggingAccess));
    TEST_ASSERT(!cm.check(200, Capability::ClipboardRead)); // Denied!

    auto stats = cm.get_audit_stats();
    TEST_ASSERT(stats.total_checks > 0);
    TEST_ASSERT(stats.total_grants >= 3);
    TEST_ASSERT(stats.total_denials >= 3);

    // PID 1 (System) has implicit full permissions
    TEST_ASSERT(cm.check(1, Capability::DisplayWindow));
    TEST_ASSERT(cm.check(1, Capability::DiagnosticsAccess));

    std::cout << "[PASS] Capability enforcement and audit stats verified.\n";
}

void test_cross_application_storage_boundary() {
    std::cout << "[Test 3] Cross-Application Storage Boundary Enforcement...\n";

    CapabilityManager cm;
    cm.bind_identity(301, "com.winios.appA");
    cm.bind_identity(302, "com.winios.appB");

    // App A has StoragePrivate only
    cm.grant(301, Capability::StoragePrivate);

    // App B has StoragePrivate AND StorageShared
    cm.grant(302, Capability::StoragePrivate);
    cm.grant(302, Capability::StorageShared);

    // 1. App A can access its own storage
    TEST_ASSERT(cm.check_storage_access(301, "com.winios.appA"));

    // 2. App A CANNOT access App B's storage (no StorageShared)
    TEST_ASSERT(!cm.check_storage_access(301, "com.winios.appB"));

    // 3. App B can access its own storage
    TEST_ASSERT(cm.check_storage_access(302, "com.winios.appB"));

    // 4. App B can access App A's shared storage because it has StorageShared
    TEST_ASSERT(cm.check_storage_access(302, "com.winios.appA"));

    std::cout << "[PASS] Cross-application storage boundary verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 224.9: PERMISSIONS & IDENTITY TESTS\n";
    std::cout << "========================================\n";

    test_identity_binding_and_tokens();
    test_capability_enforcement_and_audit();
    test_cross_application_storage_boundary();

    std::cout << "\nALL STAGE 224.9 TESTS PASSED (100%).\n";
    return 0;
}
