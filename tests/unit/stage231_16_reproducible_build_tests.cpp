#include "packaging/app_package.h"
#include "security/publisher_trust_store.h"
#include "tools/app_generator.h"
#include "loader/embedded_campaign230_workloads.inl"
#include "crypto/sha256.h"

#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #cond << std::endl; \
        std::exit(1); \
    } \
} while(0)

using namespace winios;
using namespace winios::packaging;
using namespace winios::tools;
using namespace winios::security;

static std::string compute_sha256_hex(const std::vector<uint8_t>& data) {
    crypto::Sha256 hasher;
    hasher.update(data.data(), data.size());
    uint8_t digest[32];
    hasher.finalize(digest);

    std::ostringstream ss;
    for (uint8_t b : digest) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return ss.str();
}

// 1. Test canonical file order produces identical package byte streams
static void test_canonical_file_ordering() {
    std::cout << "[Test 1] Deterministic packaging with shuffled input insertion...\n";

    AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.repro_test";
    cfg.app_name = "ReproApp";
    cfg.display_name = "Reproducible App";
    cfg.version = "1.0.0";

    const auto& bin = loader::get_embedded_campaign230_app_startup();

    // Package 1: Added in standard order
    AppPackage pkg1{};
    pkg1.manifest.bundle_id = cfg.bundle_id;
    pkg1.manifest.name = cfg.app_name;
    pkg1.manifest.display_name = cfg.display_name;
    pkg1.manifest.version = cfg.version;
    pkg1.manifest.executable_name = "bin/" + cfg.app_name;

    pkg1.add_file("bin/" + cfg.app_name, bin);
    pkg1.add_file("res/info.json", {'{', '\"', 'a', '\"', ':', '1', '}'});
    pkg1.add_file("res/icon.png", {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A});

    // Package 2: Added in reversed order
    AppPackage pkg2{};
    pkg2.manifest.bundle_id = cfg.bundle_id;
    pkg2.manifest.name = cfg.app_name;
    pkg2.manifest.display_name = cfg.display_name;
    pkg2.manifest.version = cfg.version;
    pkg2.manifest.executable_name = "bin/" + cfg.app_name;

    pkg2.add_file("res/icon.png", {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A});
    pkg2.add_file("res/info.json", {'{', '\"', 'a', '\"', ':', '1', '}'});
    pkg2.add_file("bin/" + cfg.app_name, bin);

    auto bytes1 = pkg1.pack_v3();
    auto bytes2 = pkg2.pack_v3();

    TEST_ASSERT(!bytes1.empty());
    TEST_ASSERT(bytes1.size() == bytes2.size());
    TEST_ASSERT(bytes1 == bytes2);

    std::string hash1 = compute_sha256_hex(bytes1);
    std::string hash2 = compute_sha256_hex(bytes2);
    TEST_ASSERT(hash1 == hash2);

    std::cout << "  -> Package SHA-256: " << hash1 << "\n";
    std::cout << "  -> Shuffled input order produced bit-for-bit identical packages.\n";
}

// 2. Test repeat build stability across 10 iterations
static void test_repeat_build_stability() {
    std::cout << "[Test 2] Repeat build stability across 10 iterations...\n";

    AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.stable_calc";
    cfg.app_name = "StableCalc";
    cfg.display_name = "Stable Calculator";
    const auto& bin = loader::get_embedded_campaign230_app_settings();

    std::string baseline_hash;

    for (int iter = 0; iter < 10; ++iter) {
        auto pkg = AppGenerator::generate_package(cfg, bin);
        auto raw = pkg.pack_v3();
        std::string current_hash = compute_sha256_hex(raw);

        if (iter == 0) {
            baseline_hash = current_hash;
        } else {
            TEST_ASSERT(current_hash == baseline_hash);
        }
    }

    std::cout << "  -> 10 repeat builds produced identical SHA-256: " << baseline_hash << "\n";
}

// 3. Test deterministic package signing
static void test_deterministic_signing() {
    std::cout << "[Test 3] Deterministic signature generation...\n";

    AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.signed_app";
    cfg.app_name = "SignedApp";
    cfg.display_name = "Signed Application";
    const auto& bin = loader::get_embedded_campaign230_app_docs();

    auto pkg = AppGenerator::generate_package(cfg, bin);
    auto raw = pkg.pack_v3();

    PublisherTrustStore store;
    PublisherKey pub_key{};
    pub_key.key_id = "repro-key-001";
    pub_key.publisher_id = "dev.winios.test";
    pub_key.role = PublisherRole::Developer;
    pub_key.algorithm = SignatureAlgorithm::HmacSha256;
    pub_key.key_bytes = std::vector<uint8_t>(32, 0x42);
    pub_key.valid_from = 1000;
    pub_key.valid_until = 9999999999999ULL;
    store.register_key(pub_key);

    std::vector<uint8_t> signed1 = raw;
    std::vector<uint8_t> signed2 = raw;

    TEST_ASSERT(PublisherTrustStore::sign_package(signed1, pub_key, 1000, 9999999999999ULL));
    TEST_ASSERT(PublisherTrustStore::sign_package(signed2, pub_key, 1000, 9999999999999ULL));

    TEST_ASSERT(signed1.size() == signed2.size());
    TEST_ASSERT(signed1 == signed2);

    std::string sig_hash1 = compute_sha256_hex(signed1);
    std::string sig_hash2 = compute_sha256_hex(signed2);
    TEST_ASSERT(sig_hash1 == sig_hash2);

    // Verify signature verification succeeds on both
    auto res1 = store.verify_package_signature(signed1.data(), signed1.size(), TrustPolicy::Development, 5000);
    auto res2 = store.verify_package_signature(signed2.data(), signed2.size(), TrustPolicy::Development, 5000);
    TEST_ASSERT(res1.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(res2.status == SignatureStatus::DeveloperSigned);

    std::cout << "  -> Signed package SHA-256: " << sig_hash1 << "\n";
    std::cout << "  -> Deterministic signing verified.\n";
}

// 4. Test code generator string reproducibility
static void test_generator_reproducibility() {
    std::cout << "[Test 4] AppGenerator code and script generation reproducibility...\n";

    AppTemplateConfig cfg{};
    cfg.bundle_id = "com.winios.gen_test";
    cfg.app_name = "GenTest";
    cfg.display_name = "Generation Test";

    std::string src1 = AppGenerator::generate_c_source(cfg);
    std::string src2 = AppGenerator::generate_c_source(cfg);
    TEST_ASSERT(src1 == src2);

    std::string ini1 = AppGenerator::generate_manifest_ini(cfg);
    std::string ini2 = AppGenerator::generate_manifest_ini(cfg);
    TEST_ASSERT(ini1 == ini2);

    std::string bld1 = AppGenerator::generate_build_script(cfg);
    std::string bld2 = AppGenerator::generate_build_script(cfg);
    TEST_ASSERT(bld1 == bld2);

    std::cout << "  -> Code generator templates verified 100% deterministic.\n";
}

int main() {
    std::cout << "=== Stage 231.16: Reproducible Guest Application Builds Tests ===\n";
    test_canonical_file_ordering();
    test_repeat_build_stability();
    test_deterministic_signing();
    test_generator_reproducibility();
    std::cout << "=== All Stage 231.16 Tests PASSED ===\n";
    return 0;
}
