#include "packaging/app_package.h"
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
using namespace winios::packaging;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

static AppPackage create_valid_test_package() {
    AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.limits.test";
    pkg.manifest.name = "LimitsTest";
    pkg.manifest.display_name = "Limits Test App";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.architecture = "arm64";
    pkg.manifest.executable_name = "bin/main";
    pkg.manifest.storage_quota_bytes = 10 * 1024 * 1024; // 10 MB default

    std::vector<std::uint8_t> exec_data(128, 0x55);
    pkg.add_file("bin/main", exec_data);

    std::string config_txt = "config=default\n";
    std::vector<std::uint8_t> cfg_data(config_txt.begin(), config_txt.end());
    pkg.add_file("assets/config.txt", cfg_data);

    return pkg;
}

// -----------------------------------------------------------------------------
// Test 1: Manifest Size and Integer Overflow Limits
// -----------------------------------------------------------------------------
static void test_manifest_limits() {
    std::cout << "[Stage 234.9] Test 1: Testing Manifest Size & Offset Limits...\n";

    auto orig_pkg = create_valid_test_package();
    auto raw_data = orig_pkg.pack_v3();

    // 1.1 Manifest length > 65536
    {
        auto bad_data = raw_data;
        std::uint32_t huge_len = 70000;
        std::memcpy(bad_data.data() + 32, &huge_len, 4);

        AppPackage parsed;
        auto res = AppPackage::validate_package_data(bad_data.data(), bad_data.size(), &parsed);
        TEST_ASSERT(res == PackageValidationResult::ManifestTooLarge, "Manifest > 64KB rejected");
    }

    // 1.2 Manifest offset + length integer overflow
    {
        auto bad_data = raw_data;
        std::uint64_t overflow_off = 0xFFFFFFFFFFFFFF00ULL;
        std::memcpy(bad_data.data() + 24, &overflow_off, 8);

        AppPackage parsed;
        auto res = AppPackage::validate_package_data(bad_data.data(), bad_data.size(), &parsed);
        TEST_ASSERT(res == PackageValidationResult::IntegerOverflow || res == PackageValidationResult::PayloadTruncated,
                    "Manifest offset overflow detected");
    }

    std::cout << "  -> Manifest limits verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: File Count Exceeded Limit
// -----------------------------------------------------------------------------
static void test_file_count_limit() {
    std::cout << "[Stage 234.9] Test 2: Testing File Count Limit (>256 entries)...\n";

    auto orig_pkg = create_valid_test_package();
    auto raw_data = orig_pkg.pack_v3();

    // In V3 header, file_count is at offset 36 (4 bytes)
    auto bad_data = raw_data;
    std::uint32_t excessive_count = 257;
    std::memcpy(bad_data.data() + 36, &excessive_count, 4);

    AppPackage parsed;
    auto res = AppPackage::validate_package_data(bad_data.data(), bad_data.size(), &parsed);
    TEST_ASSERT(res == PackageValidationResult::FileCountExceeded, "File count > 256 rejected");

    std::cout << "  -> File count limit verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Cumulative Payload Exceeding Storage Quota
// -----------------------------------------------------------------------------
static void test_cumulative_payload_quota() {
    std::cout << "[Stage 234.9] Test 3: Testing Storage Quota Limit Enforcement...\n";

    AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.quota.test";
    pkg.manifest.name = "QuotaTest";
    pkg.manifest.executable_name = "bin/main";
    pkg.manifest.storage_quota_bytes = 200; // Small quota: 200 bytes

    std::vector<std::uint8_t> exec_data(120, 0x55);
    pkg.add_file("bin/main", exec_data);

    // Second file brings cumulative payload to 220 bytes > 200 quota
    std::vector<std::uint8_t> extra_data(100, 0xAA);
    pkg.add_file("assets/extra.bin", extra_data);

    auto raw_data = pkg.pack_v3();

    AppPackage parsed;
    auto res = AppPackage::validate_package_data(raw_data.data(), raw_data.size(), &parsed);
    TEST_ASSERT(res == PackageValidationResult::QuotaExceeded, "Payload exceeding storage quota rejected");

    std::cout << "  -> Storage quota limit enforcement verified.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Path Length, Nesting Depth, and Formatting Limits
// -----------------------------------------------------------------------------
static void test_path_formatting_and_nesting_limits() {
    std::cout << "[Stage 234.9] Test 4: Testing Path Formatting & Nesting Depth Limits...\n";

    // 4.1 Path nesting depth > 16 levels
    {
        AppPackage pkg;
        pkg.manifest.bundle_id = "com.winios.deep.test";
        pkg.manifest.name = "DeepTest";
        pkg.manifest.executable_name = "a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/main";

        std::string deep_path = "a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/main"; // 17 slashes
        pkg.add_file(deep_path, {0x55, 0xAA});

        auto raw_data = pkg.pack_v3();
        AppPackage parsed;
        auto res = AppPackage::validate_package_data(raw_data.data(), raw_data.size(), &parsed);
        TEST_ASSERT(res == PackageValidationResult::PathTraversalAttempt, "Excessive nesting rejected");
    }

    // 4.2 Empty path component / double slashes (e.g. "bin//main")
    {
        AppPackage pkg;
        pkg.manifest.bundle_id = "com.winios.slash.test";
        pkg.manifest.name = "SlashTest";
        pkg.manifest.executable_name = "bin//main";
        pkg.add_file("bin//main", {0x55, 0xAA});

        auto raw_data = pkg.pack_v3();
        AppPackage parsed;
        auto res = AppPackage::validate_package_data(raw_data.data(), raw_data.size(), &parsed);
        TEST_ASSERT(res == PackageValidationResult::PathTraversalAttempt, "Double slashes rejected");
    }

    // 4.3 Trailing slash in path (e.g. "bin/main/")
    {
        AppPackage pkg;
        pkg.manifest.bundle_id = "com.winios.trail.test";
        pkg.manifest.name = "TrailTest";
        pkg.manifest.executable_name = "bin/main/";
        pkg.add_file("bin/main/", {0x55, 0xAA});

        auto raw_data = pkg.pack_v3();
        AppPackage parsed;
        auto res = AppPackage::validate_package_data(raw_data.data(), raw_data.size(), &parsed);
        TEST_ASSERT(res == PackageValidationResult::PathTraversalAttempt, "Trailing slash rejected");
    }

    std::cout << "  -> Path nesting and formatting limits verified.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Integer Overflow in Data Offsets
// -----------------------------------------------------------------------------
static void test_data_offset_overflow() {
    std::cout << "[Stage 234.9] Test 5: Testing Data Offset Integer Overflow...\n";

    auto orig_pkg = create_valid_test_package();
    auto raw_data = orig_pkg.pack_v3();

    // Locate offset of first file entry in directory table
    std::uint32_t mlen = 0;
    std::memcpy(&mlen, raw_data.data() + 32, 4);
    std::size_t dir_start = 48 + mlen;

    std::uint16_t plen = 0;
    std::memcpy(&plen, raw_data.data() + dir_start, 2);
    std::size_t offset_loc = dir_start + 2 + plen + 4 + 4;

    // Set data_off to 0xFFFFFFFFFFFFFFFE
    std::uint64_t overflow_off = 0xFFFFFFFFFFFFFFFEULL;
    std::memcpy(raw_data.data() + offset_loc, &overflow_off, 8);

    AppPackage parsed;
    auto res = AppPackage::validate_package_data(raw_data.data(), raw_data.size(), &parsed);
    TEST_ASSERT(res == PackageValidationResult::IntegerOverflow || res == PackageValidationResult::PayloadTruncated,
                "Data offset integer overflow detected and rejected");

    std::cout << "  -> Data offset overflow protection verified.\n";
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

    std::cout << "=================================================================\n";
    std::cout << "  Stage 234.9: Package Parser Resource Limits Test Suite         \n";
    std::cout << "=================================================================\n";

    test_manifest_limits();
    test_file_count_limit();
    test_cumulative_payload_quota();
    test_path_formatting_and_nesting_limits();
    test_data_offset_overflow();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL PACKAGE PARSER RESOURCE LIMITS TESTS PASSED!               \n";
    std::cout << "=================================================================\n";
    return 0;
}
