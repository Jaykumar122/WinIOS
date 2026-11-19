#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>

#include "security/publisher_trust_store.h"
#include "security/developer_keystore.h"

using namespace winios::security;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        std::abort(); \
    } \
} while (0)

static std::vector<std::uint8_t> make_dummy_wiospkg3() {
    std::vector<std::uint8_t> data(128, 0x41);
    std::memcpy(data.data(), "WIOSPKG3", 8);
    std::uint32_t flags = 0;
    std::memcpy(data.data() + 12, &flags, 4);
    std::uint64_t total_sz = 128;
    std::memcpy(data.data() + 16, &total_sz, 8);
    return data;
}

static void test_root_anchor() {
    PublisherTrustStore store;
    PublisherKey root = PublisherTrustStore::get_test_root_anchor_key();
    bool ok_reg = store.register_root_anchor(root);
    TEST_ASSERT(ok_reg);
    TEST_ASSERT(store.is_root_anchor(root.key_id));

    auto fetched = store.get_root_anchor();
    TEST_ASSERT(fetched.has_value());
    TEST_ASSERT(fetched->key_id == root.key_id);
    TEST_ASSERT(fetched->role == PublisherRole::RootAuthority);

    // Non-root key should return false
    TEST_ASSERT(!store.is_root_anchor("UNKNOWN-KEY"));

    std::cout << "[PASS] test_root_anchor\n";
}

static void test_multi_key_and_rotation() {
    PublisherTrustStore store;
    PublisherKey k1;
    k1.publisher_id = "com.sample.vendor";
    k1.key_id = "KEY-VEND-001";
    k1.role = PublisherRole::Developer;
    k1.algorithm = SignatureAlgorithm::Ed25519;
    k1.key_bytes.assign(32, 0x11);

    PublisherKey k2;
    k2.publisher_id = "com.sample.vendor";
    k2.key_id = "KEY-VEND-002";
    k2.role = PublisherRole::Developer;
    k2.algorithm = SignatureAlgorithm::Ed25519;
    k2.key_bytes.assign(32, 0x22);

    store.register_key(k1);
    store.register_key(k2);

    auto keys = store.get_keys_for_publisher("com.sample.vendor");
    TEST_ASSERT(keys.size() == 2);

    // Rotate k1 out for k3
    PublisherKey k3;
    k3.publisher_id = "com.sample.vendor";
    k3.key_id = "KEY-VEND-003";
    k3.role = PublisherRole::Developer;
    k3.algorithm = SignatureAlgorithm::Ed25519;
    k3.key_bytes.assign(32, 0x33);

    bool ok_rot = store.rotate_key("KEY-VEND-001", k3);
    TEST_ASSERT(ok_rot);
    TEST_ASSERT(store.is_key_revoked("KEY-VEND-001"));
    TEST_ASSERT(!store.is_key_revoked("KEY-VEND-003"));

    std::cout << "[PASS] test_multi_key_and_rotation\n";
}

static void test_revocation() {
    PublisherTrustStore store;
    PublisherKey k1;
    k1.publisher_id = "com.bad.actor";
    k1.key_id = "KEY-BAD-01";
    k1.role = PublisherRole::Developer;
    k1.algorithm = SignatureAlgorithm::Ed25519;
    k1.key_bytes.assign(32, 0x99);

    PublisherKey k2;
    k2.publisher_id = "com.bad.actor";
    k2.key_id = "KEY-BAD-02";
    k2.role = PublisherRole::Developer;
    k2.algorithm = SignatureAlgorithm::Ed25519;
    k2.key_bytes.assign(32, 0x88);

    store.register_key(k1);
    store.register_key(k2);

    TEST_ASSERT(!store.is_key_revoked("KEY-BAD-01"));
    TEST_ASSERT(!store.is_key_revoked("KEY-BAD-02"));

    // Revoke single key
    store.revoke_key("KEY-BAD-01");
    TEST_ASSERT(store.is_key_revoked("KEY-BAD-01"));
    TEST_ASSERT(!store.is_key_revoked("KEY-BAD-02"));

    // Revoke entire publisher
    store.revoke_publisher("com.bad.actor");
    TEST_ASSERT(store.is_publisher_revoked("com.bad.actor"));
    TEST_ASSERT(store.is_key_revoked("KEY-BAD-02"));

    std::cout << "[PASS] test_revocation\n";
}

static void test_ed25519_sign_and_verify_policies() {
    PublisherTrustStore store;

    // 1. Generate real developer identity and platform identity
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.mygame.dev", "MyGame Studios", "KEY-DEV-GAME-01",
        PublisherRole::Developer, 1000, 2000
    );
    auto plat_id = DeveloperKeyStore::generate_identity(
        "com.winios.platform", "WinIOS Platform", "KEY-PLAT-01",
        PublisherRole::Platform, 1000, 2000
    );

    // Register their public keys in store
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));
    store.register_key(DeveloperKeyStore::export_public_key(plat_id));

    // Sign package with developer identity
    auto pkg_dev = make_dummy_wiospkg3();
    bool ok_dev_sign = PublisherTrustStore::sign_package_ed25519(pkg_dev, dev_id, 1000, 2000);
    TEST_ASSERT(ok_dev_sign);

    // Verify developer signed under Development policy -> Trusted
    auto res_dev_devpol = store.verify_package_signature(pkg_dev.data(), pkg_dev.size(), TrustPolicy::Development, 1500);
    TEST_ASSERT(res_dev_devpol.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(res_dev_devpol.is_trusted == true);
    TEST_ASSERT(res_dev_devpol.publisher_id == dev_id.publisher_id);
    TEST_ASSERT(res_dev_devpol.key_id == dev_id.key_id);

    // Verify developer signed under StrictProduction policy -> Untrusted
    auto res_dev_prodpol = store.verify_package_signature(pkg_dev.data(), pkg_dev.size(), TrustPolicy::StrictProduction, 1500);
    TEST_ASSERT(res_dev_prodpol.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(res_dev_prodpol.is_trusted == false);

    // Sign package with platform identity
    auto pkg_plat = make_dummy_wiospkg3();
    bool ok_plat_sign = PublisherTrustStore::sign_package_ed25519(pkg_plat, plat_id, 1000, 2000);
    TEST_ASSERT(ok_plat_sign);

    // Verify platform signed under StrictProduction policy -> Trusted
    auto res_plat_prodpol = store.verify_package_signature(pkg_plat.data(), pkg_plat.size(), TrustPolicy::StrictProduction, 1500);
    TEST_ASSERT(res_plat_prodpol.status == SignatureStatus::PlatformSigned);
    TEST_ASSERT(res_plat_prodpol.is_trusted == true);

    // Expiration checks
    auto res_expired_before = store.verify_package_signature(pkg_dev.data(), pkg_dev.size(), TrustPolicy::Development, 500);
    TEST_ASSERT(res_expired_before.status == SignatureStatus::ExpiredSignature);

    auto res_expired_after = store.verify_package_signature(pkg_dev.data(), pkg_dev.size(), TrustPolicy::Development, 2500);
    TEST_ASSERT(res_expired_after.status == SignatureStatus::ExpiredSignature);

    std::cout << "[PASS] test_ed25519_sign_and_verify_policies\n";
}

static void test_algorithm_downgrade_rejection() {
    PublisherTrustStore store;

    // Register Ed25519 developer key
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.security.audit", "Security Audit Corp", "KEY-AUDIT-ED25519-01",
        PublisherRole::Developer
    );
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    // Sign package with Ed25519
    auto pkg = make_dummy_wiospkg3();
    PublisherTrustStore::sign_package_ed25519(pkg, dev_id);

    // Tamper with the trailer: change alg field from Ed25519 (2) to HmacSha256 (1)
    // The trailer starts at pkg.size() - trailer_total_len
    // Trailer structure: Magic (8B) || Version (4B) || Alg (4B) ...
    std::uint32_t trailer_len = 0;
    std::memcpy(&trailer_len, pkg.data() + pkg.size() - 4, 4);
    std::size_t alg_offset = pkg.size() - trailer_len + 8 + 4; // Magic + Version

    // Mutate alg field to 1 (HmacSha256)
    std::uint32_t downgrade_alg = 1;
    std::memcpy(pkg.data() + alg_offset, &downgrade_alg, 4);

    // Verification MUST reject with AlgorithmMismatch
    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res.status == SignatureStatus::AlgorithmMismatch);
    TEST_ASSERT(!res.is_trusted);

    std::cout << "[PASS] test_algorithm_downgrade_rejection\n";
}

static void test_tampered_payload_rejected() {
    PublisherTrustStore store;
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.tamper.test", "Tamper Test Corp", "KEY-TAMP-01"
    );
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    auto pkg = make_dummy_wiospkg3();
    PublisherTrustStore::sign_package_ed25519(pkg, dev_id);

    // Tamper with byte 0 of package
    pkg[0] ^= 0x01;

    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res.status == SignatureStatus::CorruptSignature);
    TEST_ASSERT(!res.is_trusted);

    std::cout << "[PASS] test_tampered_payload_rejected\n";
}

int main() {
    std::cout << "=== WinIOS Stage 232.6 Trust Store & Certificate Policy Tests ===\n";
    test_root_anchor();
    test_multi_key_and_rotation();
    test_revocation();
    test_ed25519_sign_and_verify_policies();
    test_algorithm_downgrade_rejection();
    test_tampered_payload_rejected();
    std::cout << "All Stage 232.6 tests passed successfully!\n";
    return 0;
}
