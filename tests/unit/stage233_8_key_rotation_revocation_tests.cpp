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

static std::vector<std::uint8_t> make_dummy_package(const std::string& name) {
    std::string text = "WIOSPKG3\napp_id=com.rotation.test\nname=" + name + "\n[files]\napp.exe:1024\n";
    std::vector<std::uint8_t> d(text.begin(), text.end());
    d.resize(256, 0xDD);
    return d;
}

// -----------------------------------------------------------------------------
// Test 1: Multi-Generation Key Rotation (Gen 1 -> Gen 2 -> Gen 3)
// -----------------------------------------------------------------------------
static void test_multigen_key_rotation() {
    std::cout << "[Test 1] Multi-Generation Key Rotation Lifecycle...\n";

    PublisherTrustStore store;

    // Gen 1
    auto gen1 = DeveloperKeyStore::generate_identity("com.corp.service", "Corp", "key-v1", PublisherRole::Developer);
    store.register_key(DeveloperKeyStore::export_public_key(gen1));

    auto pkg1 = make_dummy_package("v1");
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(pkg1, gen1), "Sign pkg 1");
    auto res1 = store.verify_package_signature(pkg1.data(), pkg1.size(), TrustPolicy::Development);
    TEST_ASSERT(res1.status == SignatureStatus::DeveloperSigned && res1.is_trusted, "Pkg 1 verified initially");

    // Rotate Gen 1 -> Gen 2
    auto gen2 = DeveloperKeyStore::generate_identity("com.corp.service", "Corp", "key-v2", PublisherRole::Developer);
    TEST_ASSERT(store.rotate_key("key-v1", DeveloperKeyStore::export_public_key(gen2)), "Rotate v1 -> v2");

    auto pkg2 = make_dummy_package("v2");
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(pkg2, gen2), "Sign pkg 2");

    // Verify pkg 2 succeeds
    auto res2 = store.verify_package_signature(pkg2.data(), pkg2.size(), TrustPolicy::Development);
    TEST_ASSERT(res2.status == SignatureStatus::DeveloperSigned && res2.is_trusted, "Pkg 2 verified with gen 2 key");

    // Verify pkg 1 now fails because gen 1 was retired/revoked
    auto res1_post = store.verify_package_signature(pkg1.data(), pkg1.size(), TrustPolicy::Development);
    TEST_ASSERT(res1_post.status == SignatureStatus::RevokedPublisher, "Pkg 1 fails with retired key-v1");

    // Rotate Gen 2 -> Gen 3
    auto gen3 = DeveloperKeyStore::generate_identity("com.corp.service", "Corp", "key-v3", PublisherRole::Developer);
    TEST_ASSERT(store.rotate_key("key-v2", DeveloperKeyStore::export_public_key(gen3)), "Rotate v2 -> v3");

    auto pkg3 = make_dummy_package("v3");
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(pkg3, gen3), "Sign pkg 3");

    // Verify pkg 3 succeeds
    auto res3 = store.verify_package_signature(pkg3.data(), pkg3.size(), TrustPolicy::Development);
    TEST_ASSERT(res3.status == SignatureStatus::DeveloperSigned && res3.is_trusted, "Pkg 3 verified with gen 3 key");

    // Verify pkg 2 now fails
    auto res2_post = store.verify_package_signature(pkg2.data(), pkg2.size(), TrustPolicy::Development);
    TEST_ASSERT(res2_post.status == SignatureStatus::RevokedPublisher, "Pkg 2 fails with retired key-v2");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Revocation Persistence Across Serialization & Deserialization
// -----------------------------------------------------------------------------
static void test_revocation_persistence() {
    std::cout << "[Test 2] Revocation Persistence Across Serialization & Deserialization...\n";

    PublisherTrustStore store1;

    auto key_active = DeveloperKeyStore::generate_identity("com.good.corp", "Good", "key-active-01", PublisherRole::Developer);
    auto key_revoked = DeveloperKeyStore::generate_identity("com.good.corp", "Good", "key-revoked-01", PublisherRole::Developer);
    auto key_bad_pub = DeveloperKeyStore::generate_identity("com.evil.corp", "Evil", "key-evil-01", PublisherRole::Developer);

    store1.register_key(DeveloperKeyStore::export_public_key(key_active));
    store1.register_key(DeveloperKeyStore::export_public_key(key_revoked));
    store1.register_key(DeveloperKeyStore::export_public_key(key_bad_pub));

    // Revoke key_revoked explicitly
    store1.revoke_key("key-revoked-01");

    // Revoke com.evil.corp publisher-wide
    store1.revoke_publisher("com.evil.corp");

    // Packages
    auto pkg_active = make_dummy_package("active");
    PublisherTrustStore::sign_package_v2(pkg_active, key_active);

    auto pkg_rev = make_dummy_package("revoked");
    PublisherTrustStore::sign_package_v2(pkg_rev, key_revoked);

    auto pkg_evil = make_dummy_package("evil");
    PublisherTrustStore::sign_package_v2(pkg_evil, key_bad_pub);

    // Verify in store1 before serialization
    TEST_ASSERT(store1.verify_package_signature(pkg_active.data(), pkg_active.size(), TrustPolicy::Development).is_trusted,
                "pkg_active trusted in store1");
    TEST_ASSERT(store1.verify_package_signature(pkg_rev.data(), pkg_rev.size(), TrustPolicy::Development).status == SignatureStatus::RevokedPublisher,
                "pkg_rev rejected in store1");
    TEST_ASSERT(store1.verify_package_signature(pkg_evil.data(), pkg_evil.size(), TrustPolicy::Development).status == SignatureStatus::RevokedPublisher,
                "pkg_evil rejected in store1");

    // Serialize to config text
    std::string config_text = store1.serialize();
    TEST_ASSERT(!config_text.empty(), "Serialization succeeded");

    // Deserialize into fresh store2
    PublisherTrustStore store2;
    TEST_ASSERT(store2.deserialize(config_text), "Deserialization succeeded");

    // Re-verify in store2
    auto res_active2 = store2.verify_package_signature(pkg_active.data(), pkg_active.size(), TrustPolicy::Development);
    TEST_ASSERT(res_active2.is_trusted && res_active2.status == SignatureStatus::DeveloperSigned,
                "pkg_active trusted in store2 after reload");

    auto res_rev2 = store2.verify_package_signature(pkg_rev.data(), pkg_rev.size(), TrustPolicy::Development);
    TEST_ASSERT(res_rev2.status == SignatureStatus::RevokedPublisher && !res_rev2.is_trusted,
                "pkg_rev rejected in store2 after reload");

    auto res_evil2 = store2.verify_package_signature(pkg_evil.data(), pkg_evil.size(), TrustPolicy::Development);
    TEST_ASSERT(res_evil2.status == SignatureStatus::RevokedPublisher && !res_evil2.is_trusted,
                "pkg_evil rejected in store2 after reload");

    TEST_ASSERT(store2.is_publisher_revoked("com.evil.corp"), "Publisher revocation persisted in store2");
    TEST_ASSERT(store2.is_key_revoked("key-revoked-01"), "Key revocation persisted in store2");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Re-registration Under Revoked Publisher Auto-Revocation
// -----------------------------------------------------------------------------
static void test_post_revocation_key_registration() {
    std::cout << "[Test 3] Post-Revocation Key Registration Defense...\n";

    PublisherTrustStore store;
    store.revoke_publisher("com.blacklisted.org");
    TEST_ASSERT(store.is_publisher_revoked("com.blacklisted.org"), "Publisher is revoked");

    // Attempt to register a brand new key under the blacklisted publisher
    auto sneaky_key = DeveloperKeyStore::generate_identity(
        "com.blacklisted.org", "Sneaky Org", "key-sneaky-01", PublisherRole::Developer);
    TEST_ASSERT(store.register_key(DeveloperKeyStore::export_public_key(sneaky_key)), "Register key succeeds");

    // But the key MUST be automatically marked revoked!
    TEST_ASSERT(store.is_key_revoked("key-sneaky-01"), "New key under blacklisted publisher is automatically revoked");

    auto pkg = make_dummy_package("sneaky");
    PublisherTrustStore::sign_package_v2(pkg, sneaky_key);

    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res.status == SignatureStatus::RevokedPublisher, "Sneaky package rejected as revoked");
    TEST_ASSERT(!res.is_trusted, "Sneaky package is not trusted");

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

    std::cout << "=== Stage 233.8: Key Rotation & Revocation Persistence Tests ===\n";

    test_multigen_key_rotation();
    test_revocation_persistence();
    test_post_revocation_key_registration();

    std::cout << "=== All Stage 233.8 Key Rotation & Revocation Persistence Tests PASSED ===\n";
    return 0;
}
