#include <iostream>
#include <vector>
#include <string>
#include <cstring>

#include "security/publisher_trust_store.h"
#include "security/security_utils.h"
#include "packaging/app_package.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Trust Store Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::security;

// RFC 4231 Test Vector 1 for HMAC-SHA256
static void test_hmac_rfc4231_vector() {
    std::cout << "[Stage 231.4] Testing RFC 4231 HMAC-SHA256 test vector...\n";

    // Key = 20 bytes of 0x0b
    std::vector<std::uint8_t> key(20, 0x0b);
    std::string data = "Hi There";

    std::uint8_t out[32];
    hmac_sha256(key.data(), key.size(), data.data(), data.size(), out);

    // Expected: b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7
    const std::uint8_t expected[32] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
        0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
        0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
    };

    TEST_ASSERT(constant_time_equals(out, expected, 32));
}

// Helper to create a dummy WIOSPKG3 container
static std::vector<std::uint8_t> create_dummy_package() {
    winios::packaging::AppPackage pkg;
    pkg.manifest.bundle_id = "com.sample.testapp";
    pkg.manifest.name = "TestApp";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/main";
    pkg.manifest.format_version = "3.0";

    std::vector<std::uint8_t> dummy_bin = {0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01};
    pkg.add_file("bin/main", dummy_bin);

    return pkg.pack_v3();
}

static void test_platform_signed_verification() {
    std::cout << "[Stage 231.4] Testing platform-signed package...\n";

    PublisherTrustStore store = PublisherTrustStore::create_default_test_store();
    PublisherKey plat_key = PublisherTrustStore::get_test_platform_key();

    std::vector<std::uint8_t> pkg = create_dummy_package();
    TEST_ASSERT(PublisherTrustStore::sign_package(pkg, plat_key));

    // 1. Verify in Development mode
    auto res_dev = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res_dev.status == SignatureStatus::PlatformSigned);
    TEST_ASSERT(res_dev.is_trusted == true);
    TEST_ASSERT(res_dev.publisher_id == plat_key.publisher_id);
    TEST_ASSERT(res_dev.key_id == plat_key.key_id);
    TEST_ASSERT(!res_dev.digest_hex.empty());

    // 2. Verify in StrictProduction mode
    auto res_prod = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(res_prod.status == SignatureStatus::PlatformSigned);
    TEST_ASSERT(res_prod.is_trusted == true);
}

static void test_developer_signed_verification() {
    std::cout << "[Stage 231.4] Testing developer-signed package...\n";

    PublisherTrustStore store = PublisherTrustStore::create_default_test_store();
    PublisherKey dev_key = PublisherTrustStore::get_test_developer_key();

    std::vector<std::uint8_t> pkg = create_dummy_package();
    TEST_ASSERT(PublisherTrustStore::sign_package(pkg, dev_key));

    // 1. In Development mode: should be trusted as DeveloperSigned
    auto res_dev = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res_dev.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(res_dev.is_trusted == true);
    TEST_ASSERT(res_dev.publisher_id == dev_key.publisher_id);

    // 2. In StrictProduction mode: should NOT be trusted
    auto res_prod = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(res_prod.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(res_prod.is_trusted == false);
}

static void test_unsigned_package() {
    std::cout << "[Stage 231.4] Testing unsigned package...\n";

    PublisherTrustStore store = PublisherTrustStore::create_default_test_store();
    std::vector<std::uint8_t> pkg = create_dummy_package();

    auto res_dev = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res_dev.status == SignatureStatus::Unsigned);
    TEST_ASSERT(res_dev.is_trusted == true); // Allowed in dev

    auto res_prod = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(res_prod.status == SignatureStatus::Unsigned);
    TEST_ASSERT(res_prod.is_trusted == false); // Rejected in strict production
}

static void test_revoked_key_and_publisher() {
    std::cout << "[Stage 231.4] Testing revocation handling...\n";

    PublisherTrustStore store = PublisherTrustStore::create_default_test_store();
    PublisherKey revoked_key = PublisherTrustStore::get_test_revoked_key();

    std::vector<std::uint8_t> pkg = create_dummy_package();
    TEST_ASSERT(PublisherTrustStore::sign_package(pkg, revoked_key));

    // Should be rejected in all policies
    auto res_dev = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res_dev.status == SignatureStatus::RevokedPublisher);
    TEST_ASSERT(res_dev.is_trusted == false);

    auto res_prod = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(res_prod.status == SignatureStatus::RevokedPublisher);
    TEST_ASSERT(res_prod.is_trusted == false);

    // Dynamically revoke the dev key
    PublisherKey dev_key = PublisherTrustStore::get_test_developer_key();
    std::vector<std::uint8_t> dev_pkg = create_dummy_package();
    PublisherTrustStore::sign_package(dev_pkg, dev_key);

    TEST_ASSERT(store.revoke_key(dev_key.key_id));
    auto res_after_revoke = store.verify_package_signature(dev_pkg.data(), dev_pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res_after_revoke.status == SignatureStatus::RevokedPublisher);
    TEST_ASSERT(res_after_revoke.is_trusted == false);

    // Dynamically revoke a publisher ID
    PublisherKey plat_key = PublisherTrustStore::get_test_platform_key();
    std::vector<std::uint8_t> plat_pkg = create_dummy_package();
    PublisherTrustStore::sign_package(plat_pkg, plat_key);

    TEST_ASSERT(store.revoke_publisher(plat_key.publisher_id));
    auto res_pub_revoke = store.verify_package_signature(plat_pkg.data(), plat_pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res_pub_revoke.status == SignatureStatus::RevokedPublisher);
    TEST_ASSERT(res_pub_revoke.is_trusted == false);
}

static void test_tampered_payload_and_corrupt_signature() {
    std::cout << "[Stage 231.4] Testing tampered payload and corrupt signatures...\n";

    PublisherTrustStore store = PublisherTrustStore::create_default_test_store();
    PublisherKey plat_key = PublisherTrustStore::get_test_platform_key();

    std::vector<std::uint8_t> pkg = create_dummy_package();
    PublisherTrustStore::sign_package(pkg, plat_key);

    // 1. Tamper with payload byte (e.g. at index 50)
    std::vector<std::uint8_t> tampered_pkg = pkg;
    tampered_pkg[50] ^= 0xFF;
    auto res_tampered = store.verify_package_signature(tampered_pkg.data(), tampered_pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res_tampered.status == SignatureStatus::CorruptSignature);
    TEST_ASSERT(res_tampered.is_trusted == false);

    // 2. Corrupt signature byte in trailer
    std::vector<std::uint8_t> corrupt_sig_pkg = pkg;
    // Signature bytes are right before the last 4 bytes (trailer_len)
    corrupt_sig_pkg[corrupt_sig_pkg.size() - 10] ^= 0xAA;
    auto res_corrupt = store.verify_package_signature(corrupt_sig_pkg.data(), corrupt_sig_pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res_corrupt.status == SignatureStatus::CorruptSignature);
    TEST_ASSERT(res_corrupt.is_trusted == false);
}

static void test_expired_signature() {
    std::cout << "[Stage 231.4] Testing timestamp expiration...\n";

    PublisherTrustStore store = PublisherTrustStore::create_default_test_store();
    PublisherKey dev_key = PublisherTrustStore::get_test_developer_key();

    std::vector<std::uint8_t> pkg = create_dummy_package();
    // Valid between timestamp 1000 and 2000
    PublisherTrustStore::sign_package(pkg, dev_key, 1000, 2000);

    // Verify at t=1500 -> Valid
    auto res_valid = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development, 1500);
    TEST_ASSERT(res_valid.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(res_valid.is_trusted == true);

    // Verify at t=500 -> Not yet valid
    auto res_early = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development, 500);
    TEST_ASSERT(res_early.status == SignatureStatus::ExpiredSignature);
    TEST_ASSERT(res_early.is_trusted == false);

    // Verify at t=2500 -> Expired
    auto res_late = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development, 2500);
    TEST_ASSERT(res_late.status == SignatureStatus::ExpiredSignature);
    TEST_ASSERT(res_late.is_trusted == false);
}

static void test_trust_store_serialization() {
    std::cout << "[Stage 231.4] Testing trust store serialization/deserialization...\n";

    PublisherTrustStore original = PublisherTrustStore::create_default_test_store();
    std::string text = original.serialize();
    TEST_ASSERT(!text.empty());

    PublisherTrustStore restored;
    TEST_ASSERT(restored.deserialize(text));
    TEST_ASSERT(restored.total_keys() == original.total_keys());

    PublisherKey plat = PublisherTrustStore::get_test_platform_key();
    auto maybe_plat = restored.get_key(plat.key_id);
    TEST_ASSERT(maybe_plat.has_value());
    TEST_ASSERT(maybe_plat->role == PublisherRole::Platform);
    TEST_ASSERT(constant_time_equals(maybe_plat->key_bytes.data(), plat.key_bytes.data(), plat.key_bytes.size()));

    PublisherKey revoked = PublisherTrustStore::get_test_revoked_key();
    TEST_ASSERT(restored.is_key_revoked(revoked.key_id));
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 231.4: Publisher Trust Store Tests          \n";
    std::cout << "====================================================\n";

    test_hmac_rfc4231_vector();
    test_platform_signed_verification();
    test_developer_signed_verification();
    test_unsigned_package();
    test_revoked_key_and_publisher();
    test_tampered_payload_and_corrupt_signature();
    test_expired_signature();
    test_trust_store_serialization();

    std::cout << "\n[Stage 231.4] ALL TESTS PASSED SUCCESSFULLY.\n";
    return 0;
}
