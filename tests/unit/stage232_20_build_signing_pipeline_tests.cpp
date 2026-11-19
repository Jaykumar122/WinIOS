#include "packaging/app_package.h"
#include "security/publisher_trust_store.h"
#include "security/developer_keystore.h"
#include "crypto/sha512.h"
#include "crypto/ed25519.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <algorithm>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #cond << std::endl; \
        std::exit(1); \
    } \
} while(0)

using namespace winios;
using namespace winios::packaging;
using namespace winios::security;

static AppPackage create_sample_package() {
    AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.pipeline.test";
    pkg.manifest.name = "PipelineTestApp";
    pkg.manifest.display_name = "Pipeline Test Application";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/main";
    pkg.manifest.min_sdk_version_u32 = 0x00020400;
    pkg.manifest.permissions = {"SettingsRead", "DocumentRead"};

    std::vector<uint8_t> dummy_bin = {
        0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    pkg.add_file("bin/main", dummy_bin);

    std::string config_txt = "theme=dark\nlog_level=debug\n";
    pkg.add_file("res/config.txt", std::vector<uint8_t>(config_txt.begin(), config_txt.end()));

    std::string icon_dat = "DUMMY_ICON_PNG_HEADER";
    pkg.add_file("res/icon.png", std::vector<uint8_t>(icon_dat.begin(), icon_dat.end()));

    return pkg;
}

// 1. Unsigned Canonical Packaging Determinism
static void test_unsigned_canonical_packaging_determinism() {
    std::cout << "[Stage 232.20] Testing unsigned canonical packaging determinism...\n";

    auto pkg1 = create_sample_package();

    // Create pkg2 with inverted file insertion order
    AppPackage pkg2;
    pkg2.manifest = pkg1.manifest;
    pkg2.manifest.files.clear();

    std::string icon_dat = "DUMMY_ICON_PNG_HEADER";
    pkg2.add_file("res/icon.png", std::vector<uint8_t>(icon_dat.begin(), icon_dat.end()));

    std::string config_txt = "theme=dark\nlog_level=debug\n";
    pkg2.add_file("res/config.txt", std::vector<uint8_t>(config_txt.begin(), config_txt.end()));

    std::vector<uint8_t> dummy_bin = {
        0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    pkg2.add_file("bin/main", dummy_bin);

    auto bytes1 = pkg1.pack_v3();
    auto bytes2 = pkg2.pack_v3();

    TEST_ASSERT(!bytes1.empty());
    TEST_ASSERT(bytes1.size() == bytes2.size());
    TEST_ASSERT(bytes1 == bytes2);

    std::cout << "  -> pack_v3 produces bit-for-bit identical canonical output regardless of insertion order.\n";
}

// 2. Deterministic Signing Pipeline & Bit-for-Bit Reproducibility
static void test_deterministic_signing_pipeline() {
    std::cout << "[Stage 232.20] Testing deterministic signing pipeline...\n";

    auto pkg = create_sample_package();
    auto unsigned_bytes = pkg.pack_v3();

    auto identity = DeveloperKeyStore::generate_identity(
        "com.pipeline.developer", "Pipeline Labs", "KEY-PIPE-001"
    );
    TEST_ASSERT(identity.is_valid());

    uint64_t fixed_timestamp = 1735689600; // 2025-01-01 00:00:00 UTC
    uint64_t valid_until = fixed_timestamp + 365 * 86400;
    uint32_t flags = 0x00000001;

    auto signed_bytes1 = unsigned_bytes;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(signed_bytes1, identity, flags, fixed_timestamp, valid_until));

    auto signed_bytes2 = unsigned_bytes;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(signed_bytes2, identity, flags, fixed_timestamp, valid_until));

    TEST_ASSERT(signed_bytes1.size() == signed_bytes2.size());
    TEST_ASSERT(signed_bytes1 == signed_bytes2);

    std::cout << "  -> Asymmetric package signing is 100% deterministic with fixed timestamps.\n";
}

// 3. Unsigned Artifact Recovery & Exact Byte Consistency
static void test_unsigned_artifact_recovery() {
    std::cout << "[Stage 232.20] Testing unsigned artifact recovery from signed package...\n";

    auto pkg = create_sample_package();
    auto original_unsigned = pkg.pack_v3();

    auto identity = DeveloperKeyStore::generate_identity(
        "com.pipeline.developer", "Pipeline Labs", "KEY-PIPE-002"
    );
    TEST_ASSERT(identity.is_valid());

    auto signed_bytes = original_unsigned;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(signed_bytes, identity, 0, 1000, 2000));
    TEST_ASSERT(signed_bytes.size() > original_unsigned.size());

    // Recover trailer size from the last 4 bytes of signed_bytes (trailer length field)
    uint32_t trailer_len = 0;
    std::memcpy(&trailer_len, signed_bytes.data() + signed_bytes.size() - 4, 4);
    TEST_ASSERT(trailer_len > 0 && trailer_len < signed_bytes.size());

    // Strip trailer
    size_t unsigned_size = signed_bytes.size() - trailer_len;
    TEST_ASSERT(unsigned_size == original_unsigned.size());

    std::vector<uint8_t> recovered_unsigned(signed_bytes.begin(), signed_bytes.begin() + unsigned_size);

    // For WIOSPKG3, the header at offset 12 has bit 0 (Signed) set and offset 16 has total_sz updated
    // Restoring the unsigned header fields produces the exact bit-for-bit unsigned package
    if (recovered_unsigned.size() >= 24 && std::memcmp(recovered_unsigned.data(), "WIOSPKG3", 8) == 0) {
        uint32_t flags = 0;
        std::memcpy(&flags, recovered_unsigned.data() + 12, sizeof(flags));
        flags &= ~0x01; // Clear Signed bit
        std::memcpy(recovered_unsigned.data() + 12, &flags, sizeof(flags));

        uint64_t orig_sz = static_cast<uint64_t>(unsigned_size);
        std::memcpy(recovered_unsigned.data() + 16, &orig_sz, sizeof(orig_sz));
    }

    TEST_ASSERT(recovered_unsigned == original_unsigned);

    std::cout << "  -> Stripping signature trailer recovers exact bit-for-bit original unsigned package.\n";
}

// 4. Multi-Key Signing and Independent Verification
static void test_multi_key_signing_independence() {
    std::cout << "[Stage 232.20] Testing multi-key signing and trust store verification...\n";

    auto pkg = create_sample_package();
    auto unsigned_bytes = pkg.pack_v3();

    auto id_a = DeveloperKeyStore::generate_identity("com.dev.a", "Org A", "KEY-DEV-A");
    auto id_b = DeveloperKeyStore::generate_identity("com.dev.b", "Org B", "KEY-DEV-B");
    TEST_ASSERT(id_a.is_valid() && id_b.is_valid());

    auto signed_a = unsigned_bytes;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(signed_a, id_a, 0, 1000, 2000));

    auto signed_b = unsigned_bytes;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(signed_b, id_b, 0, 1000, 2000));

    PublisherTrustStore store;
    store.register_key(DeveloperKeyStore::export_public_key(id_a));
    store.register_key(DeveloperKeyStore::export_public_key(id_b));

    // Both must verify successfully against the trust store
    auto res_a = store.verify_package_signature(signed_a.data(), signed_a.size(), TrustPolicy::Development, 1500);
    TEST_ASSERT(res_a.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(res_a.is_trusted);
    TEST_ASSERT(res_a.key_id == "KEY-DEV-A");

    auto res_b = store.verify_package_signature(signed_b.data(), signed_b.size(), TrustPolicy::Development, 1500);
    TEST_ASSERT(res_b.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(res_b.is_trusted);
    TEST_ASSERT(res_b.key_id == "KEY-DEV-B");

    // Digests of both packages must be identical because the underlying unsigned package is identical!
    TEST_ASSERT(res_a.digest_hex == res_b.digest_hex);

    std::cout << "  -> Distinct key signatures bind independently to identical unsigned canonical digests.\n";
}

// 5. Tamper Detection in Signing Pipeline
static void test_pipeline_tamper_detection() {
    std::cout << "[Stage 232.20] Testing pipeline tamper detection...\n";

    auto pkg = create_sample_package();
    auto unsigned_bytes = pkg.pack_v3();

    auto id = DeveloperKeyStore::generate_identity("com.dev.c", "Org C", "KEY-DEV-C");
    TEST_ASSERT(id.is_valid());

    auto signed_bytes = unsigned_bytes;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(signed_bytes, id, 0, 1000, 2000));

    PublisherTrustStore store;
    store.register_key(DeveloperKeyStore::export_public_key(id));

    // Case 1: Tamper with 1 byte in unsigned payload area
    {
        auto tampered = signed_bytes;
        tampered[30] ^= 0xFF; // Mutate byte in header/payload
        auto res = store.verify_package_signature(tampered.data(), tampered.size(), TrustPolicy::Development, 1500);
        TEST_ASSERT(res.status == SignatureStatus::CorruptSignature);
    }

    // Case 2: Tamper with 1 byte in signature area of trailer
    {
        auto tampered = signed_bytes;
        tampered[tampered.size() - 20] ^= 0x01;
        auto res = store.verify_package_signature(tampered.data(), tampered.size(), TrustPolicy::Development, 1500);
        TEST_ASSERT(res.status == SignatureStatus::CorruptSignature);
    }

    // Case 3: Replay against expired validity window
    {
        auto res_expired = store.verify_package_signature(signed_bytes.data(), signed_bytes.size(), TrustPolicy::Development, 2500);
        TEST_ASSERT(res_expired.status == SignatureStatus::ExpiredSignature);
    }

    // Case 4: Key revocation
    {
        store.revoke_key("KEY-DEV-C");
        auto res_revoked = store.verify_package_signature(signed_bytes.data(), signed_bytes.size(), TrustPolicy::Development, 1500);
        TEST_ASSERT(res_revoked.status == SignatureStatus::RevokedPublisher);
    }

    std::cout << "  -> Pipeline tamper detection, expiration, and revocation verified.\n";
}

int main() {
    std::cout << "=== Stage 232.20: Deterministic Build & Signing Pipeline Tests ===\n";
    test_unsigned_canonical_packaging_determinism();
    test_deterministic_signing_pipeline();
    test_unsigned_artifact_recovery();
    test_multi_key_signing_independence();
    test_pipeline_tamper_detection();
    std::cout << "=== All Stage 232.20 Tests PASSED ===\n";
    return 0;
}
