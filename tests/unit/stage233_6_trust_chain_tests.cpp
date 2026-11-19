#include "security/publisher_trust_store.h"
#include "security/developer_keystore.h"
#include "crypto/ed25519.h"
#include "crypto/sha256.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

#if defined(_WIN32)
#include <crtdbg.h>
#endif

using namespace winios;
using namespace winios::security;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

// Helper to create a dummy package payload
static std::vector<std::uint8_t> make_dummy_package(const std::string& app_id) {
    std::string text = "WIOSPKG3\napp_id=" + app_id + "\nversion=1.0.0\n[files]\nmain.bin:100\n";
    std::vector<std::uint8_t> data(text.begin(), text.end());
    // Pad to at least 128 bytes
    data.resize(128, 0xAB);
    return data;
}

// -----------------------------------------------------------------------------
// Test 1: Role Hierarchy & Policy Enforcement (Root, Platform, Developer)
// -----------------------------------------------------------------------------
static void test_role_hierarchy_and_policy() {
    std::cout << "[Test 1] Role Hierarchy & Policy Enforcement...\n";

    PublisherTrustStore store;

    // 1. Root Anchor Identity
    auto root_id = DeveloperKeyStore::generate_identity(
        "com.apple.research.root", "WinIOS Root CA", "root-key-01", PublisherRole::RootAuthority);
    auto root_pub = DeveloperKeyStore::export_public_key(root_id);
    TEST_ASSERT(store.register_root_anchor(root_pub), "Register root anchor");
    TEST_ASSERT(store.is_root_anchor("root-key-01"), "Confirm root anchor key ID");

    // 2. Platform Identity
    auto plat_id = DeveloperKeyStore::generate_identity(
        "com.winios.platform", "WinIOS Platform Team", "plat-key-01", PublisherRole::Platform);
    auto plat_pub = DeveloperKeyStore::export_public_key(plat_id);
    TEST_ASSERT(store.register_key(plat_pub), "Register platform key");

    // 3. Developer Identity
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.thirdparty.app", "Independent Developer", "dev-key-01", PublisherRole::Developer);
    auto dev_pub = DeveloperKeyStore::export_public_key(dev_id);
    TEST_ASSERT(store.register_key(dev_pub), "Register developer key");

    // Sign packages
    auto pkg_root = make_dummy_package("com.apple.kernel");
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(pkg_root, root_id), "Sign root package");

    auto pkg_plat = make_dummy_package("com.winios.systemservice");
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(pkg_plat, plat_id), "Sign platform package");

    auto pkg_dev = make_dummy_package("com.thirdparty.app");
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(pkg_dev, dev_id), "Sign developer package");

    // Verify under Development Policy
    auto res_root_dev = store.verify_package_signature(pkg_root.data(), pkg_root.size(), TrustPolicy::Development);
    TEST_ASSERT(res_root_dev.status == SignatureStatus::PlatformSigned, "Root is PlatformSigned");
    TEST_ASSERT(res_root_dev.is_trusted, "Root trusted under Development");

    auto res_plat_dev = store.verify_package_signature(pkg_plat.data(), pkg_plat.size(), TrustPolicy::Development);
    TEST_ASSERT(res_plat_dev.status == SignatureStatus::PlatformSigned, "Platform is PlatformSigned");
    TEST_ASSERT(res_plat_dev.is_trusted, "Platform trusted under Development");

    auto res_dev_dev = store.verify_package_signature(pkg_dev.data(), pkg_dev.size(), TrustPolicy::Development);
    TEST_ASSERT(res_dev_dev.status == SignatureStatus::DeveloperSigned, "Developer is DeveloperSigned");
    TEST_ASSERT(res_dev_dev.is_trusted, "Developer trusted under Development");

    // Verify under StrictProduction Policy
    auto res_root_prod = store.verify_package_signature(pkg_root.data(), pkg_root.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(res_root_prod.status == SignatureStatus::PlatformSigned && res_root_prod.is_trusted,
                "Root trusted under StrictProduction");

    auto res_plat_prod = store.verify_package_signature(pkg_plat.data(), pkg_plat.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(res_plat_prod.status == SignatureStatus::PlatformSigned && res_plat_prod.is_trusted,
                "Platform trusted under StrictProduction");

    auto res_dev_prod = store.verify_package_signature(pkg_dev.data(), pkg_dev.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(res_dev_prod.status == SignatureStatus::DeveloperSigned, "Developer is DeveloperSigned in Prod");
    TEST_ASSERT(!res_dev_prod.is_trusted, "Developer NOT trusted under StrictProduction");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Key & Publisher Revocation Correctness
// -----------------------------------------------------------------------------
static void test_key_and_publisher_revocation() {
    std::cout << "[Test 2] Key and Publisher Revocation...\n";

    PublisherTrustStore store;

    auto dev1_id = DeveloperKeyStore::generate_identity(
        "com.compromised.corp", "Compromised Corp", "key-comp-01", PublisherRole::Developer);
    store.register_key(DeveloperKeyStore::export_public_key(dev1_id));

    auto dev2_id = DeveloperKeyStore::generate_identity(
        "com.compromised.corp", "Compromised Corp", "key-comp-02", PublisherRole::Developer);
    store.register_key(DeveloperKeyStore::export_public_key(dev2_id));

    auto pkg1 = make_dummy_package("com.compromised.app1");
    PublisherTrustStore::sign_package_v2(pkg1, dev1_id);

    auto pkg2 = make_dummy_package("com.compromised.app2");
    PublisherTrustStore::sign_package_v2(pkg2, dev2_id);

    // 1. Initially valid
    auto res1 = store.verify_package_signature(pkg1.data(), pkg1.size(), TrustPolicy::Development);
    TEST_ASSERT(res1.status == SignatureStatus::DeveloperSigned && res1.is_trusted, "Key 1 initially valid");

    // 2. Revoke key-comp-01 only
    TEST_ASSERT(store.revoke_key("key-comp-01"), "Revoke key 1");
    TEST_ASSERT(store.is_key_revoked("key-comp-01"), "Key 1 confirmed revoked");
    TEST_ASSERT(!store.is_key_revoked("key-comp-02"), "Key 2 not revoked yet");

    auto res1_rev = store.verify_package_signature(pkg1.data(), pkg1.size(), TrustPolicy::Development);
    TEST_ASSERT(res1_rev.status == SignatureStatus::RevokedPublisher, "Package 1 rejected as revoked key");
    TEST_ASSERT(!res1_rev.is_trusted, "Revoked key is not trusted");

    // Key 2 should still be valid
    auto res2 = store.verify_package_signature(pkg2.data(), pkg2.size(), TrustPolicy::Development);
    TEST_ASSERT(res2.status == SignatureStatus::DeveloperSigned && res2.is_trusted, "Key 2 still valid");

    // 3. Revoke entire publisher
    TEST_ASSERT(store.revoke_publisher("com.compromised.corp"), "Revoke publisher");
    TEST_ASSERT(store.is_publisher_revoked("com.compromised.corp"), "Publisher confirmed revoked");
    TEST_ASSERT(store.is_key_revoked("key-comp-02"), "Key 2 revoked via publisher revocation");

    auto res2_rev = store.verify_package_signature(pkg2.data(), pkg2.size(), TrustPolicy::Development);
    TEST_ASSERT(res2_rev.status == SignatureStatus::RevokedPublisher, "Package 2 rejected after publisher revoke");
    TEST_ASSERT(!res2_rev.is_trusted, "Package 2 not trusted");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Key Rotation
// -----------------------------------------------------------------------------
static void test_key_rotation() {
    std::cout << "[Test 3] Key Rotation...\n";

    PublisherTrustStore store;

    auto old_id = DeveloperKeyStore::generate_identity(
        "com.example.service", "Example Service", "key-gen-1", PublisherRole::Developer);
    store.register_key(DeveloperKeyStore::export_public_key(old_id));

    auto pkg_old = make_dummy_package("com.example.service");
    PublisherTrustStore::sign_package_v2(pkg_old, old_id);

    // Rotate to new key
    auto new_id = DeveloperKeyStore::generate_identity(
        "com.example.service", "Example Service", "key-gen-2", PublisherRole::Developer);
    TEST_ASSERT(store.rotate_key("key-gen-1", DeveloperKeyStore::export_public_key(new_id)), "Rotate key");

    // Old key must now be revoked
    TEST_ASSERT(store.is_key_revoked("key-gen-1"), "Old key is revoked");
    auto res_old = store.verify_package_signature(pkg_old.data(), pkg_old.size(), TrustPolicy::Development);
    TEST_ASSERT(res_old.status == SignatureStatus::RevokedPublisher, "Old package revoked");

    // New key package verifies
    auto pkg_new = make_dummy_package("com.example.service");
    PublisherTrustStore::sign_package_v2(pkg_new, new_id);
    auto res_new = store.verify_package_signature(pkg_new.data(), pkg_new.size(), TrustPolicy::Development);
    TEST_ASSERT(res_new.status == SignatureStatus::DeveloperSigned && res_new.is_trusted, "New package verified");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Temporal Validity & Expiration
// -----------------------------------------------------------------------------
static void test_temporal_validity() {
    std::cout << "[Test 4] Temporal Validity & Expiration...\n";

    PublisherTrustStore store;

    std::uint64_t valid_from = 1700000000;
    std::uint64_t valid_until = 1710000000;

    auto id = DeveloperKeyStore::generate_identity(
        "com.example.timed", "Timed Org", "key-timed-01",
        PublisherRole::Developer, valid_from, valid_until);
    store.register_key(DeveloperKeyStore::export_public_key(id));

    auto pkg = make_dummy_package("com.example.timed");
    PublisherTrustStore::sign_package_v2(pkg, id, 0, valid_from, valid_until);

    // 1. Before valid_from -> Expired/NotYetValid
    auto res_early = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development, 1699999999);
    TEST_ASSERT(res_early.status == SignatureStatus::ExpiredSignature, "Early timestamp rejected");
    TEST_ASSERT(!res_early.is_trusted, "Early package not trusted");

    // 2. Inside window -> Valid
    auto res_ok = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development, 1705000000);
    TEST_ASSERT(res_ok.status == SignatureStatus::DeveloperSigned, "In-window timestamp accepted");
    TEST_ASSERT(res_ok.is_trusted, "In-window package trusted");

    // 3. After valid_until -> Expired
    auto res_late = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development, 1710000001);
    TEST_ASSERT(res_late.status == SignatureStatus::ExpiredSignature, "Expired timestamp rejected");
    TEST_ASSERT(!res_late.is_trusted, "Expired package not trusted");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Unknown Key & Publisher Mismatch Attacks
// -----------------------------------------------------------------------------
static void test_unknown_key_and_publisher_mismatch() {
    std::cout << "[Test 5] Unknown Key & Publisher Mismatch Attacks...\n";

    PublisherTrustStore store;

    auto reg_id = DeveloperKeyStore::generate_identity(
        "com.legit.publisher", "Legit Org", "key-legit-01", PublisherRole::Developer);
    store.register_key(DeveloperKeyStore::export_public_key(reg_id));

    // Sign with an unregistered key
    auto unreg_id = DeveloperKeyStore::generate_identity(
        "com.rogue.publisher", "Rogue Org", "key-unregistered", PublisherRole::Developer);
    auto pkg_unreg = make_dummy_package("com.rogue.app");
    PublisherTrustStore::sign_package_v2(pkg_unreg, unreg_id);

    auto res_unreg = store.verify_package_signature(pkg_unreg.data(), pkg_unreg.size(), TrustPolicy::Development);
    TEST_ASSERT(res_unreg.status == SignatureStatus::UnknownKey, "Unregistered key rejected");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 6: Serialization & Deserialization Roundtrip
// -----------------------------------------------------------------------------
static void test_serialization_roundtrip() {
    std::cout << "[Test 6] Trust Store Serialization Roundtrip...\n";

    PublisherTrustStore store1;

    auto root = DeveloperKeyStore::generate_identity("com.root", "Root CA", "key-root", PublisherRole::RootAuthority);
    store1.register_root_anchor(DeveloperKeyStore::export_public_key(root));

    auto plat = DeveloperKeyStore::generate_identity("com.platform", "Plat Inc", "key-plat", PublisherRole::Platform);
    store1.register_key(DeveloperKeyStore::export_public_key(plat));

    auto dev = DeveloperKeyStore::generate_identity("com.developer", "Dev LLC", "key-dev", PublisherRole::Developer);
    store1.register_key(DeveloperKeyStore::export_public_key(dev));

    store1.revoke_key("key-dev");
    store1.revoke_publisher("com.badactor");

    std::string serialized = store1.serialize();
    TEST_ASSERT(!serialized.empty(), "Serialization succeeded");

    PublisherTrustStore store2;
    TEST_ASSERT(store2.deserialize(serialized), "Deserialization succeeded");

    TEST_ASSERT(store2.total_keys() == 3, "Total keys match (3)");
    TEST_ASSERT(store2.is_key_revoked("key-dev"), "Revoked key persisted");
    TEST_ASSERT(store2.is_publisher_revoked("com.badactor"), "Revoked publisher persisted");

    auto loaded_plat = store2.get_key("key-plat");
    TEST_ASSERT(loaded_plat.has_value(), "Loaded platform key exists");
    TEST_ASSERT(loaded_plat->role == PublisherRole::Platform, "Loaded platform role preserved");
    TEST_ASSERT(loaded_plat->key_bytes == plat.public_key, "Key bytes preserved");

    std::cout << "  -> Passed.\n";
}

int main() {
#if defined(_WIN32)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
#endif

    std::cout << "=== Stage 233.6: Publisher Trust Chain Tests ===\n";

    test_role_hierarchy_and_policy();
    test_key_and_publisher_revocation();
    test_key_rotation();
    test_temporal_validity();
    test_unknown_key_and_publisher_mismatch();
    test_serialization_roundtrip();

    std::cout << "=== All Stage 233.6 Publisher Trust Chain Tests PASSED ===\n";
    return 0;
}
