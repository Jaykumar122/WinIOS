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

static std::vector<std::uint8_t> make_test_pkg() {
    std::string content = "WIOSPKG3\nversion=3.0\napp_id=com.winios.trailer.test\n[data]\nsome_important_binary_payload\n";
    std::vector<std::uint8_t> data(content.begin(), content.end());
    data.resize(256, 0xEE);
    return data;
}

// -----------------------------------------------------------------------------
// Test 1: Full Valid WIOSAUTH v2 Signature Roundtrip
// -----------------------------------------------------------------------------
static void test_valid_wiosauth_v2_roundtrip() {
    std::cout << "[Test 1] Valid WIOSAUTH v2 Signature Roundtrip...\n";

    PublisherTrustStore store;
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.winios.auth", "Auth Test Corp", "key-auth-01", PublisherRole::Developer);
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    auto pkg = make_test_pkg();
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(pkg, dev_id, 0x00000004), "Sign package v2");

    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res.status == SignatureStatus::DeveloperSigned, "Status DeveloperSigned");
    TEST_ASSERT(res.is_trusted, "Package is trusted");
    TEST_ASSERT(res.trailer_version == 2, "Trailer version is 2");
    TEST_ASSERT(res.trailer_flags == 0x00000004, "Trailer flags preserved");
    TEST_ASSERT(res.publisher_id == "com.winios.auth", "Publisher ID matches");
    TEST_ASSERT(res.key_id == "key-auth-01", "Key ID matches");
    TEST_ASSERT(!res.digest_hex.empty(), "Digest hex populated");
    TEST_ASSERT(!res.signature_hex.empty(), "Signature hex populated");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Payload Tampering Defense (Content Digest Mismatch)
// -----------------------------------------------------------------------------
static void test_payload_tampering_detection() {
    std::cout << "[Test 2] Payload Tampering Defense...\n";

    PublisherTrustStore store;
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.winios.auth", "Auth Test Corp", "key-auth-02", PublisherRole::Developer);
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    auto pkg = make_test_pkg();
    PublisherTrustStore::sign_package_v2(pkg, dev_id);

    // Tamper with payload byte (before trailer)
    pkg[50] ^= 0x01;

    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res.status == SignatureStatus::CorruptSignature, "Tampered payload rejected");
    TEST_ASSERT(!res.is_trusted, "Tampered package is not trusted");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Trailer Magic Tampering
// -----------------------------------------------------------------------------
static void test_trailer_magic_tampering() {
    std::cout << "[Test 3] Trailer Magic Tampering...\n";

    PublisherTrustStore store;
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.winios.auth", "Auth Test Corp", "key-auth-03", PublisherRole::Developer);
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    auto pkg = make_test_pkg();
    PublisherTrustStore::sign_package_v2(pkg, dev_id);

    // Read trailer total length from last 4 bytes
    std::uint32_t tlen = 0;
    std::memcpy(&tlen, pkg.data() + pkg.size() - 4, 4);
    std::size_t trailer_start = pkg.size() - tlen;

    // Corrupt magic bytes
    pkg[trailer_start] = 'X';
    pkg[trailer_start + 1] = 'X';

    // Must be treated as unsigned
    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(!res.is_trusted, "Corrupted magic not trusted in StrictProduction");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Trailer Metadata Tampering (Trailer Flags Bit Flip)
// -----------------------------------------------------------------------------
static void test_trailer_flags_tampering() {
    std::cout << "[Test 4] Trailer Flags Tampering...\n";

    PublisherTrustStore store;
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.winios.auth", "Auth Test Corp", "key-auth-04", PublisherRole::Developer);
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    auto pkg = make_test_pkg();
    PublisherTrustStore::sign_package_v2(pkg, dev_id, 0);

    std::uint32_t tlen = 0;
    std::memcpy(&tlen, pkg.data() + pkg.size() - 4, 4);
    std::size_t trailer_start = pkg.size() - tlen;

    // Locate trailer_flags (after magic(8) + ver(4) + alg(4) + pub_len(2) + pub + key_len(2) + key + time(16))
    std::size_t flags_offset = trailer_start + 8 + 4 + 4 + 2 + dev_id.publisher_id.size() + 2 + dev_id.key_id.size() + 16;
    pkg[flags_offset] ^= 0x01; // flip a bit in flags

    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res.status == SignatureStatus::CorruptSignature, "Tampered flags rejected");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Key ID Substitution Attack
// -----------------------------------------------------------------------------
static void test_key_id_substitution_attack() {
    std::cout << "[Test 5] Key ID Substitution Attack...\n";

    PublisherTrustStore store;
    auto dev_a = DeveloperKeyStore::generate_identity("com.org.a", "Org A", "key-alph1", PublisherRole::Developer);
    auto dev_b = DeveloperKeyStore::generate_identity("com.org.b", "Org B", "key-beta1", PublisherRole::Developer);
    store.register_key(DeveloperKeyStore::export_public_key(dev_a));
    store.register_key(DeveloperKeyStore::export_public_key(dev_b));

    auto pkg = make_test_pkg();
    PublisherTrustStore::sign_package_v2(pkg, dev_a);

    // Tamper key_id in trailer to key-beta1 (same string length: 9 chars)
    std::uint32_t tlen = 0;
    std::memcpy(&tlen, pkg.data() + pkg.size() - 4, 4);
    std::size_t trailer_start = pkg.size() - tlen;

    std::size_t key_str_offset = trailer_start + 8 + 4 + 4 + 2 + dev_a.publisher_id.size() + 2;
    std::memcpy(pkg.data() + key_str_offset, "key-beta1", 9);

    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(!res.is_trusted && res.status == SignatureStatus::CorruptSignature, "Key substitution rejected");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 6: Algorithm Downgrade Attack
// -----------------------------------------------------------------------------
static void test_algorithm_downgrade_attack() {
    std::cout << "[Test 6] Algorithm Downgrade Attack...\n";

    PublisherTrustStore store;
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.winios.auth", "Auth Test Corp", "key-auth-06", PublisherRole::Developer);
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    auto pkg = make_test_pkg();
    PublisherTrustStore::sign_package_v2(pkg, dev_id);

    std::uint32_t tlen = 0;
    std::memcpy(&tlen, pkg.data() + pkg.size() - 4, 4);
    std::size_t trailer_start = pkg.size() - tlen;

    // Alg field is at trailer_start + 8 (magic) + 4 (version) = trailer_start + 12
    // Overwrite alg from Ed25519 (2) to HmacSha256 (1)
    std::uint32_t fake_alg = 1;
    std::memcpy(pkg.data() + trailer_start + 12, &fake_alg, 4);

    auto res = store.verify_package_signature(pkg.data(), pkg.size(), TrustPolicy::Development);
    TEST_ASSERT(res.status == SignatureStatus::AlgorithmMismatch, "Algorithm downgrade rejected");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 7: Signature Malleability & Bit Flipping
// -----------------------------------------------------------------------------
static void test_signature_bit_flipping() {
    std::cout << "[Test 7] Signature Malleability & Bit-Flipping Tests...\n";

    PublisherTrustStore store;
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.winios.auth", "Auth Test Corp", "key-auth-07", PublisherRole::Developer);
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    auto pkg = make_test_pkg();
    PublisherTrustStore::sign_package_v2(pkg, dev_id);

    // Signature is located at pkg.size() - 4 (tlen) - 64 (sig)
    std::size_t sig_start = pkg.size() - 4 - 64;

    for (std::size_t i = 0; i < 64; ++i) {
        auto tampered_pkg = pkg;
        tampered_pkg[sig_start + i] ^= 0x80;
        auto res = store.verify_package_signature(tampered_pkg.data(), tampered_pkg.size(), TrustPolicy::Development);
        TEST_ASSERT(res.status == SignatureStatus::CorruptSignature, "Signature bit-flip rejected");
    }

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

    std::cout << "=== Stage 233.7: WIOSAUTH Conformance Tests ===\n";

    test_valid_wiosauth_v2_roundtrip();
    test_payload_tampering_detection();
    test_trailer_magic_tampering();
    test_trailer_flags_tampering();
    test_key_id_substitution_attack();
    test_algorithm_downgrade_attack();
    test_signature_bit_flipping();

    std::cout << "=== All Stage 233.7 WIOSAUTH Conformance Tests PASSED ===\n";
    return 0;
}
