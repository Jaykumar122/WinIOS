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
    std::vector<std::uint8_t> data(256, 0x42);
    std::memcpy(data.data(), "WIOSPKG3", 8);
    std::uint32_t flags = 0;
    std::memcpy(data.data() + 12, &flags, 4);
    std::uint64_t total_sz = 256;
    std::memcpy(data.data() + 16, &total_sz, 8);
    return data;
}

static void test_wiosauth_v2_ed25519() {
    PublisherTrustStore store;
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.v2.dev", "V2 Developer", "KEY-V2-ED25519-01",
        PublisherRole::Developer, 1000, 2000
    );
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    auto pkg = make_dummy_wiospkg3();
    std::uint32_t test_flags = 0x00000005; // e.g. flags
    bool ok_sign = PublisherTrustStore::sign_package_v2(pkg, dev_id, test_flags, 1000, 2000);
    TEST_ASSERT(ok_sign);

    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development, 1500);
    TEST_ASSERT(res.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(res.is_trusted == true);
    TEST_ASSERT(res.trailer_version == 2);
    TEST_ASSERT(res.trailer_flags == test_flags);
    TEST_ASSERT(res.publisher_id == "com.v2.dev");
    TEST_ASSERT(res.key_id == "KEY-V2-ED25519-01");
    TEST_ASSERT(!res.digest_hex.empty());
    TEST_ASSERT(!res.signature_hex.empty());

    std::cout << "[PASS] test_wiosauth_v2_ed25519\n";
}

static void test_wiosauth_v2_hmac() {
    PublisherTrustStore store;
    PublisherKey key;
    key.publisher_id = "com.v2.hmac";
    key.organization_name = "HMAC Org";
    key.key_id = "KEY-V2-HMAC-01";
    key.role = PublisherRole::Developer;
    key.algorithm = SignatureAlgorithm::HmacSha256;
    key.key_bytes.assign(32, 0x77);

    store.register_key(key);

    auto pkg = make_dummy_wiospkg3();
    std::uint32_t test_flags = 0x00000002;
    bool ok_sign = PublisherTrustStore::sign_package_hmac_v2(pkg, key, test_flags, 1000, 2000);
    TEST_ASSERT(ok_sign);

    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development, 1500);
    TEST_ASSERT(res.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(res.is_trusted == true);
    TEST_ASSERT(res.trailer_version == 2);
    TEST_ASSERT(res.trailer_flags == test_flags);
    TEST_ASSERT(res.publisher_id == "com.v2.hmac");
    TEST_ASSERT(res.key_id == "KEY-V2-HMAC-01");

    std::cout << "[PASS] test_wiosauth_v2_hmac\n";
}

static void test_backward_compatibility_v1() {
    PublisherTrustStore store;
    PublisherKey key = PublisherTrustStore::get_test_developer_key();
    store.register_key(key);

    auto pkg = make_dummy_wiospkg3();
    // Legacy v1 signing
    bool ok_sign = PublisherTrustStore::sign_package(pkg, key, 0, 0);
    TEST_ASSERT(ok_sign);

    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(res.is_trusted == true);
    TEST_ASSERT(res.trailer_version == 1);
    TEST_ASSERT(res.trailer_flags == 0);
    TEST_ASSERT(res.publisher_id == key.publisher_id);
    TEST_ASSERT(res.key_id == key.key_id);

    std::cout << "[PASS] test_backward_compatibility_v1\n";
}

static void test_tamper_content_digest() {
    PublisherTrustStore store;
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.digest.test", "Digest Test", "KEY-DIGEST-01"
    );
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    auto pkg = make_dummy_wiospkg3();
    PublisherTrustStore::sign_package_v2(pkg, dev_id);

    // Tamper with payload byte (before trailer)
    pkg[20] ^= 0xFF;

    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res.status == SignatureStatus::CorruptSignature);
    TEST_ASSERT(!res.is_trusted);

    std::cout << "[PASS] test_tamper_content_digest\n";
}

static void test_tamper_trailer_metadata_fails_signature() {
    PublisherTrustStore store;
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.metadata.test", "Metadata Test", "KEY-META-01",
        PublisherRole::Developer, 1000, 2000
    );
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    auto pkg = make_dummy_wiospkg3();
    std::uint32_t flags = 0x01;
    PublisherTrustStore::sign_package_v2(pkg, dev_id, flags, 1000, 2000);

    // Locate trailer
    std::uint32_t trailer_len = 0;
    std::memcpy(&trailer_len, pkg.data() + pkg.size() - 4, 4);
    std::size_t trailer_start = pkg.size() - trailer_len;

    // Mutate the flags field in trailer (at trailer_start + 8(magic) + 4(ver) + 4(alg) + 2 + pub + 2 + key + 16(time))
    std::size_t flags_offset = trailer_start + 8 + 4 + 4 + 2 + dev_id.publisher_id.size() + 2 + dev_id.key_id.size() + 16;
    std::uint32_t tampered_flags = 0x99;
    std::memcpy(pkg.data() + flags_offset, &tampered_flags, 4);

    // Content digest still matches the payload!
    // BUT the signature domain cryptographically binds trailer_flags, so Ed25519 verify MUST FAIL!
    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development, 1500);
    TEST_ASSERT(res.status == SignatureStatus::CorruptSignature);
    TEST_ASSERT(!res.is_trusted);

    std::cout << "[PASS] test_tamper_trailer_metadata_fails_signature\n";
}

static void test_unknown_version_rejected() {
    PublisherTrustStore store;
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.ver.test", "Version Test", "KEY-VER-01"
    );
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    auto pkg = make_dummy_wiospkg3();
    PublisherTrustStore::sign_package_v2(pkg, dev_id);

    std::uint32_t trailer_len = 0;
    std::memcpy(&trailer_len, pkg.data() + pkg.size() - 4, 4);
    std::size_t trailer_start = pkg.size() - trailer_len;

    // Change version from 2 to 3
    std::uint32_t unsupported_version = 3;
    std::memcpy(pkg.data() + trailer_start + 8, &unsupported_version, 4);

    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res.status == SignatureStatus::CorruptSignature);
    TEST_ASSERT(!res.is_trusted);

    std::cout << "[PASS] test_unknown_version_rejected\n";
}

int main() {
    std::cout << "=== WinIOS Stage 232.7 WIOSAUTH v2 Tests ===\n";
    test_wiosauth_v2_ed25519();
    test_wiosauth_v2_hmac();
    test_backward_compatibility_v1();
    test_tamper_content_digest();
    test_tamper_trailer_metadata_fails_signature();
    test_unknown_version_rejected();
    std::cout << "All Stage 232.7 tests passed successfully!\n";
    return 0;
}
