#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <limits>

#include "packaging/app_package.h"
#include "security/publisher_trust_store.h"
#include "security/security_utils.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Parser Hardening Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::packaging;
using namespace winios::security;

// Helper to create a valid baseline package
static std::vector<std::uint8_t> create_valid_test_package() {
    AppPackage pkg;
    pkg.manifest.bundle_id = "com.sample.harden";
    pkg.manifest.name = "HardenApp";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/main";
    pkg.manifest.format_version = "3.0";

    std::vector<std::uint8_t> bin_data = {0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x02, 0x03, 0x04};
    pkg.add_file("bin/main", bin_data);
    std::vector<std::uint8_t> asset_data = {'A', 'S', 'S', 'E', 'T'};
    pkg.add_file("res/asset.dat", asset_data);

    return pkg.pack_v3();
}

static void test_integer_overflow_rejection() {
    std::cout << "[Stage 231.5] Testing integer overflow rejection in WIOSPKG...\n";

    // 1.1 Manifest offset + length overflow in V3 header
    {
        std::vector<std::uint8_t> pkg = create_valid_test_package();
        // manifest_off is at offset 24 (8 bytes), manifest_len is at offset 32 (4 bytes)
        std::uint64_t huge_off = std::numeric_limits<std::uint64_t>::max() - 10;
        std::memcpy(pkg.data() + 24, &huge_off, 8);
        std::uint32_t len = 100;
        std::memcpy(pkg.data() + 32, &len, 4);

        AppPackage out_pkg;
        auto res = AppPackage::validate_package_data(pkg.data(), pkg.size(), &out_pkg);
        TEST_ASSERT(res == PackageValidationResult::IntegerOverflow);
    }

    // 1.2 File entry offset + size overflow in directory table
    {
        std::vector<std::uint8_t> pkg = create_valid_test_package();
        // Manifest is at offset 48
        std::uint32_t mlen = 0;
        std::memcpy(&mlen, pkg.data() + 32, 4);
        std::size_t dir_start = 48 + mlen;

        // First entry: path length at dir_start (2 bytes), path, then type(4), flags(4), offset(8), size(8)
        std::uint16_t plen = 0;
        std::memcpy(&plen, pkg.data() + dir_start, 2);
        std::size_t offset_loc = dir_start + 2 + plen + 4 + 4;

        std::uint64_t huge_data_off = std::numeric_limits<std::uint64_t>::max() - 5;
        std::uint64_t data_sz = 10;
        std::memcpy(pkg.data() + offset_loc, &huge_data_off, 8);
        std::memcpy(pkg.data() + offset_loc + 8, &data_sz, 8);

        AppPackage out_pkg;
        auto res = AppPackage::validate_package_data(pkg.data(), pkg.size(), &out_pkg);
        TEST_ASSERT(res == PackageValidationResult::IntegerOverflow);
    }
}

static void test_duplicate_path_collision_rejection() {
    std::cout << "[Stage 231.5] Testing duplicate and case-collision path rejection...\n";

    // 2.1 Case-insensitive collision: "bin/main" vs "BIN/MAIN"
    {
        AppPackage pkg;
        pkg.manifest.bundle_id = "com.sample.collision";
        pkg.manifest.name = "CollisionApp";
        pkg.manifest.version = "1.0.0";
        pkg.manifest.executable_name = "bin/main";
        pkg.manifest.format_version = "3.0";

        std::vector<std::uint8_t> d1 = {1, 2, 3, 4};
        std::vector<std::uint8_t> d2 = {5, 6, 7, 8};
        pkg.add_file("bin/main", d1);
        pkg.add_file("BIN/MAIN", d2); // Case collision on Windows host!

        auto packed = pkg.pack_v3();
        AppPackage out_pkg;
        auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &out_pkg);
        TEST_ASSERT(res == PackageValidationResult::DuplicateFileEntry);
    }
}

static void test_null_char_and_control_injection() {
    std::cout << "[Stage 231.5] Testing null char and control code injection rejection...\n";

    // 3.1 Null character injection in file path
    {
        AppPackage pkg;
        pkg.manifest.bundle_id = "com.sample.nullpath";
        pkg.manifest.name = "NullPathApp";
        pkg.manifest.version = "1.0.0";
        pkg.manifest.executable_name = "bin/main";
        pkg.manifest.format_version = "3.0";

        std::vector<std::uint8_t> d = {1, 2, 3, 4};
        pkg.add_file("bin/main", d);

        std::string sneaky_path = "res/icon.png";
        sneaky_path[4] = '\0'; // Sneaky null byte inside path
        pkg.add_file(sneaky_path, d);

        auto packed = pkg.pack_v3();
        AppPackage out_pkg;
        auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &out_pkg);
        TEST_ASSERT(res == PackageValidationResult::PathTraversalAttempt);
    }

    // 3.2 Control character in file path
    {
        AppPackage pkg;
        pkg.manifest.bundle_id = "com.sample.ctrlpath";
        pkg.manifest.name = "CtrlPathApp";
        pkg.manifest.version = "1.0.0";
        pkg.manifest.executable_name = "bin/main";
        pkg.manifest.format_version = "3.0";

        std::vector<std::uint8_t> d = {1, 2, 3, 4};
        pkg.add_file("bin/main", d);
        pkg.add_file("res/\x01poison.bin", d);

        auto packed = pkg.pack_v3();
        AppPackage out_pkg;
        auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &out_pkg);
        TEST_ASSERT(res == PackageValidationResult::PathTraversalAttempt);
    }

    // 3.3 Embedded null in manifest text
    {
        std::vector<std::uint8_t> pkg = create_valid_test_package();
        // manifest text starts at offset 48
        pkg[60] = '\0'; // Inject null into manifest string
        AppPackage out_pkg;
        auto res = AppPackage::validate_package_data(pkg.data(), pkg.size(), &out_pkg);
        TEST_ASSERT(res == PackageValidationResult::InvalidManifest);
    }
}

static void test_declared_size_and_overlapping_entries() {
    std::cout << "[Stage 231.5] Testing declared size exceeding physical buffer & overlapping entries...\n";

    // 4.1 Declared size > physical buffer
    {
        std::vector<std::uint8_t> pkg = create_valid_test_package();
        std::uint32_t mlen = 0;
        std::memcpy(&mlen, pkg.data() + 32, 4);
        std::size_t dir_start = 48 + mlen;

        std::uint16_t plen = 0;
        std::memcpy(&plen, pkg.data() + dir_start, 2);
        std::size_t offset_loc = dir_start + 2 + plen + 4 + 4;

        // Set data_sz to 1 MB while total package is ~1 KB
        std::uint64_t huge_sz = 1024 * 1024;
        std::memcpy(pkg.data() + offset_loc + 8, &huge_sz, 8);

        AppPackage out_pkg;
        auto res = AppPackage::validate_package_data(pkg.data(), pkg.size(), &out_pkg);
        TEST_ASSERT(res == PackageValidationResult::PayloadTruncated || res == PackageValidationResult::FileCountExceeded);
    }

    // 4.2 Overlapping entries: entry 2 offset points inside entry 1
    {
        std::vector<std::uint8_t> pkg = create_valid_test_package();
        std::uint32_t mlen = 0;
        std::memcpy(&mlen, pkg.data() + 32, 4);
        std::size_t dir_start = 48 + mlen;

        // Entry 1
        std::uint16_t plen1 = 0;
        std::memcpy(&plen1, pkg.data() + dir_start, 2);
        std::size_t e1_loc = dir_start + 2 + plen1 + 4 + 4;
        std::uint64_t e1_off = 0;
        std::memcpy(&e1_off, pkg.data() + e1_loc, 8);

        // Entry 2
        std::size_t e2_start = dir_start + 2 + plen1 + 4 + 4 + 8 + 8 + 32;
        std::uint16_t plen2 = 0;
        std::memcpy(&plen2, pkg.data() + e2_start, 2);
        std::size_t e2_loc = e2_start + 2 + plen2 + 4 + 4;

        // Set Entry 2's offset to match Entry 1's offset
        std::memcpy(pkg.data() + e2_loc, &e1_off, 8);

        AppPackage out_pkg;
        auto res = AppPackage::validate_package_data(pkg.data(), pkg.size(), &out_pkg);
        TEST_ASSERT(res == PackageValidationResult::OverlappingEntries);
    }
}

static void test_hardened_trust_evaluation() {
    std::cout << "[Stage 231.5] Testing integrated trust store evaluation in evaluate_trust...\n";

    PublisherTrustStore store = PublisherTrustStore::create_default_test_store();
    PublisherKey plat_key = PublisherTrustStore::get_test_platform_key();
    PublisherKey dev_key = PublisherTrustStore::get_test_developer_key();

    // 5.1 Platform-signed package evaluation
    {
        std::vector<std::uint8_t> pkg = create_valid_test_package();
        PublisherTrustStore::sign_package(pkg, plat_key);

        auto trust = AppPackage::evaluate_trust(pkg.data(), pkg.size(), store, false);
        TEST_ASSERT(trust.integrity_verified == true);
        TEST_ASSERT(trust.trust_level == PackageTrustLevel::PlatformSigned);
        TEST_ASSERT(!trust.signature_hex.empty());
    }

    // 5.2 Developer-signed package in dev mode vs strict mode
    {
        std::vector<std::uint8_t> pkg = create_valid_test_package();
        PublisherTrustStore::sign_package(pkg, dev_key);

        auto trust_dev = AppPackage::evaluate_trust(pkg.data(), pkg.size(), store, true);
        TEST_ASSERT(trust_dev.integrity_verified == true);
        TEST_ASSERT(trust_dev.trust_level == PackageTrustLevel::DeveloperLocal);

        auto trust_prod = AppPackage::evaluate_trust(pkg.data(), pkg.size(), store, false);
        TEST_ASSERT(trust_prod.trust_level == PackageTrustLevel::Unsigned);
    }

    // 5.3 Corrupt signature envelope evaluation
    {
        std::vector<std::uint8_t> pkg = create_valid_test_package();
        PublisherTrustStore::sign_package(pkg, plat_key);
        // Tamper with payload
        pkg[100] ^= 0xAA;

        auto trust = AppPackage::evaluate_trust(pkg.data(), pkg.size(), store, true);
        TEST_ASSERT(trust.integrity_verified == false);
        TEST_ASSERT(trust.trust_level == PackageTrustLevel::Unsigned);
    }

    // 5.4 Legacy string "PLATFORM_ROOT_A16_TRUSTED" compatibility preserved
    {
        AppPackage pkg;
        pkg.manifest.bundle_id = "com.sample.legacy";
        pkg.manifest.name = "LegacyApp";
        pkg.manifest.version = "1.0.0";
        pkg.manifest.executable_name = "bin/main";
        pkg.manifest.signature_sha256 = "PLATFORM_ROOT_A16_TRUSTED";
        std::vector<std::uint8_t> d = {1, 2, 3};
        pkg.add_file("bin/main", d);

        auto trust = AppPackage::evaluate_trust(pkg, false);
        TEST_ASSERT(trust.trust_level == PackageTrustLevel::PlatformSigned);
    }
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 231.5: WIOSPKG Parser Hardening Tests       \n";
    std::cout << "====================================================\n";

    test_integer_overflow_rejection();
    test_duplicate_path_collision_rejection();
    test_null_char_and_control_injection();
    test_declared_size_and_overlapping_entries();
    test_hardened_trust_evaluation();

    std::cout << "\n[Stage 231.5] ALL TESTS PASSED SUCCESSFULLY.\n";
    return 0;
}
