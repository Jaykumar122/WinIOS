#include "security/publisher_trust_store.h"
#include "security/developer_keystore.h"
#include "packaging/app_package.h"
#include "crypto/ed25519.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::security;
using namespace winios::packaging;
using namespace winios::crypto;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

static std::vector<std::uint8_t> make_dummy_package() {
    AppPackage pkg{};
    pkg.manifest.format_version = 3;
    pkg.manifest.bundle_id = "com.winios.coverage";
    pkg.manifest.name = "CoverageApp";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/main";

    pkg.add_file("bin/main", { 0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01 }); // Mach-O 64 magic
    return pkg.pack_v3();
}

static void test_metadata_field_mutations() {
    std::cout << "[Stage 234.8] Test 1: Testing Independent Mutation of Every Covered Field...\n";

    PublisherTrustStore trust_store;
    DeveloperIdentity dev_id = DeveloperKeyStore::generate_identity("com.winios.covpub", "Coverage Org", "key-cov-01", PublisherRole::Platform);
    PublisherKey pub_key = DeveloperKeyStore::export_public_key(dev_id);
    TEST_ASSERT(trust_store.register_key(pub_key), "Register platform key");

    auto raw_pkg = make_dummy_package();
    const std::size_t raw_payload_size = raw_pkg.size();

    std::vector<std::uint8_t> signed_pkg = raw_pkg;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(signed_pkg, dev_id), "Sign package with v2 trailer");

    // Baseline verify must succeed
    auto base_res = trust_store.verify_package_signature(signed_pkg.data(), signed_pkg.size(), TrustPolicy::StrictProduction);
    TEST_ASSERT(base_res.is_trusted, "Baseline signed package verifies");

    const std::size_t trailer_offset = raw_payload_size;
    TEST_ASSERT(signed_pkg.size() > trailer_offset + 32, "Trailer is attached");

    // 1. Mutate payload byte 0 (tests payload integrity)
    {
        auto mut_pkg = signed_pkg;
        mut_pkg[0] ^= 0x01;
        auto res = trust_store.verify_package_signature(mut_pkg.data(), mut_pkg.size(), TrustPolicy::StrictProduction);
        TEST_ASSERT(!res.is_trusted, "Payload mutation rejected");
    }

    // 2. Mutate magic (offset trailer_offset + 0..7)
    {
        auto mut_pkg = signed_pkg;
        mut_pkg[trailer_offset] ^= 0x01;
        auto res = trust_store.verify_package_signature(mut_pkg.data(), mut_pkg.size(), TrustPolicy::StrictProduction);
        TEST_ASSERT(!res.is_trusted, "Corrupted trailer magic rejected");
    }

    // 3. Mutate version (offset trailer_offset + 8..11)
    {
        auto mut_pkg = signed_pkg;
        mut_pkg[trailer_offset + 8] = 0x99;
        auto res = trust_store.verify_package_signature(mut_pkg.data(), mut_pkg.size(), TrustPolicy::StrictProduction);
        TEST_ASSERT(!res.is_trusted, "Corrupted trailer version rejected");
    }

    // 4. Mutate algorithm (offset trailer_offset + 12..15)
    {
        auto mut_pkg = signed_pkg;
        mut_pkg[trailer_offset + 12] = 0x01; // Change to HMAC-SHA256
        auto res = trust_store.verify_package_signature(mut_pkg.data(), mut_pkg.size(), TrustPolicy::StrictProduction);
        TEST_ASSERT(!res.is_trusted, "Algorithm tampering rejected");
    }

    // 5. Mutate trailer total length at very end (last 4 bytes)
    {
        auto mut_pkg = signed_pkg;
        mut_pkg.back() ^= 0xFF;
        auto res = trust_store.verify_package_signature(mut_pkg.data(), mut_pkg.size(), TrustPolicy::StrictProduction);
        TEST_ASSERT(!res.is_trusted, "Corrupted trailer length footer rejected");
    }

    std::cout << "  -> All covered fields verified independently: Mutating any field breaks authenticity.\n";
}

static void test_structural_trailer_adversarial() {
    std::cout << "[Stage 234.8] Test 2: Structural Trailer Anomalies (Truncation, Trailing Garbage)...\n";

    PublisherTrustStore trust_store;
    DeveloperIdentity dev_id = DeveloperKeyStore::generate_identity("com.winios.struct", "Struct Org", "key-struct-01", PublisherRole::Platform);
    PublisherKey pub_key = DeveloperKeyStore::export_public_key(dev_id);
    TEST_ASSERT(trust_store.register_key(pub_key), "Register platform key");

    auto raw_pkg = make_dummy_package();
    std::vector<std::uint8_t> signed_pkg = raw_pkg;
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(signed_pkg, dev_id), "Sign package");

    // Case 2a: Truncated trailer (cut off last 16 bytes)
    {
        auto mut_pkg = signed_pkg;
        mut_pkg.resize(mut_pkg.size() - 16);
        auto res = trust_store.verify_package_signature(mut_pkg.data(), mut_pkg.size(), TrustPolicy::StrictProduction);
        TEST_ASSERT(!res.is_trusted, "Truncated trailer rejected");
    }

    // Case 2b: Trailing garbage appended after trailer footer
    {
        auto mut_pkg = signed_pkg;
        mut_pkg.push_back(0xDE);
        mut_pkg.push_back(0xAD);
        mut_pkg.push_back(0xBE);
        mut_pkg.push_back(0xEF);
        auto res = trust_store.verify_package_signature(mut_pkg.data(), mut_pkg.size(), TrustPolicy::StrictProduction);
        TEST_ASSERT(!res.is_trusted, "Trailing garbage after trailer rejected");
    }

    // Case 2c: Zero-length package
    {
        auto res = trust_store.verify_package_signature(nullptr, 0, TrustPolicy::StrictProduction);
        TEST_ASSERT(!res.is_trusted, "Zero-length package safely rejected");
    }

    std::cout << "  -> All structural trailer anomalies cleanly handled and rejected.\n";
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  Stage 234.8: WIOSAUTH v2 Signature Coverage Test Suite         \n";
    std::cout << "=================================================================\n";

    test_metadata_field_mutations();
    test_structural_trailer_adversarial();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL WIOSAUTH COVERAGE TESTS PASSED!                            \n";
    std::cout << "=================================================================\n";
    return 0;
}
