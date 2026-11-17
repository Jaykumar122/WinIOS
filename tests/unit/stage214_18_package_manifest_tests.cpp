// ============================================================================
// Stage 214.18: Application Manifest and Package Format Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <vector>

#include "loader/app_package.h"
#include "loader/linux_image_loader.h"

using namespace winios::loader;

namespace {

int g_failures = 0;

#define PKG_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[PKG_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [PKG_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_manifest_validation() {
    std::cout << "[Stage 214.18] Verifying Application Package Manifest Validation...\n";

    // 1. Valid manifest
    AppManifest valid_m{};
    valid_m.app_id = "com.winios.counter";
    valid_m.display_name = "Counter";
    valid_m.binary_path = "/Applications/Counter.app/counter";
    valid_m.min_runtime_version = 0x00021400;

    auto res1 = AppPackageValidator::validate_manifest(valid_m, 0x00021400);
    PKG_CHECK(res1 == PackageValidationResult::Valid, "Valid manifest approved");

    // 2. Invalid bundle ID (no dot)
    AppManifest bad_id = valid_m;
    bad_id.app_id = "counterapp";
    PKG_CHECK(AppPackageValidator::validate_manifest(bad_id) == PackageValidationResult::InvalidAppId,
              "Invalid app ID rejected");

    // 3. Missing display name
    AppManifest missing_name = valid_m;
    missing_name.display_name = "";
    PKG_CHECK(AppPackageValidator::validate_manifest(missing_name) == PackageValidationResult::MissingRequiredField,
              "Missing display name rejected");

    // 4. Incompatible runtime version
    AppManifest future_m = valid_m;
    future_m.min_runtime_version = 0x00030000; // Requires version 3.0
    PKG_CHECK(AppPackageValidator::validate_manifest(future_m, 0x00021400) == PackageValidationResult::RuntimeVersionIncompatible,
              "Future runtime version requirement rejected");

    // 5. Exceeded memory budget (> 64 MB)
    AppManifest huge_mem = valid_m;
    huge_mem.max_memory_bytes = 128 * 1024 * 1024;
    PKG_CHECK(AppPackageValidator::validate_manifest(huge_mem) == PackageValidationResult::ResourceBudgetExceeded,
              "Excessive memory budget request rejected");
}

void test_binary_hash_verification() {
    std::cout << "[Stage 214.18] Verifying Package Binary SHA-256 Validation...\n";

    std::vector<std::uint8_t> binary_payload = {'M', 'a', 'c', 'h', '-', 'O', ' ', 'A', 'R', 'M', '6', '4'};
    std::string expected_hash = LinuxImageLoader::compute_sha256(binary_payload.data(), binary_payload.size());

    AppManifest manifest{};
    manifest.app_id = "com.winios.verified";
    manifest.display_name = "Verified App";
    manifest.binary_path = "/Applications/Verified.app/verified";
    manifest.binary_sha256 = expected_hash;

    // Check with authentic binary
    auto res_auth = AppPackageValidator::validate_binary_hash(manifest, binary_payload.data(), binary_payload.size());
    PKG_CHECK(res_auth == PackageValidationResult::Valid, "Authentic binary matches manifest SHA-256");

    // Check with tampered binary
    std::vector<std::uint8_t> tampered_payload = binary_payload;
    tampered_payload[0] = 'X'; // Modify 1 byte
    auto res_tamper = AppPackageValidator::validate_binary_hash(manifest, tampered_payload.data(), tampered_payload.size());
    PKG_CHECK(res_tamper == PackageValidationResult::BinaryHashMismatch,
              "Tampered binary rejected with BinaryHashMismatch");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.18: Package Manifest Tests        \n";
    std::cout << "====================================================\n";

    test_manifest_validation();
    test_binary_hash_verification();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL PACKAGE TESTS PASSED" : "PACKAGE FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
