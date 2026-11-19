#include "packaging/app_package.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <algorithm>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::packaging;

static void test_manifest_v3_serialization() {
    PackageManifest m{};
    m.format_version = "3.0";
    m.bundle_id = "com.winios.demo";
    m.name = "DemoApp";
    m.display_name = "Demo Application";
    m.version = "1.2.3";
    m.executable_name = "bin/demo";
    m.min_sdk_version_u32 = 0x00020400;
    m.target_abi_version = "2.4";
    m.resource_limits.max_memory_bytes = 64 * 1024 * 1024;
    m.resource_limits.max_instruction_budget = 50000000ULL;
    m.resource_limits.max_open_handles = 128;
    m.supported_doc_types = {"text/plain", ".txt", ".md"};
    m.entry_symbol = "_start";
    m.entry_stack_size = 131072;
    m.category = "Developer";
    m.author = "WinIOS Team";
    m.description = "Test developer application";

    FileEntry fe{};
    fe.path = "bin/demo";
    fe.data = {0x01, 0x02, 0x03, 0x04};
    m.files.push_back(fe);

    TEST_ASSERT(m.is_valid());

    std::string serialized = m.serialize();
    PackageManifest out{};
    bool ok = PackageManifest::deserialize(serialized, out);
    TEST_ASSERT(ok);

    TEST_ASSERT(out.format_version == "3.0");
    TEST_ASSERT(out.bundle_id == "com.winios.demo");
    TEST_ASSERT(out.display_name == "Demo Application");
    TEST_ASSERT(out.min_sdk_version_u32 == 0x00020400);
    TEST_ASSERT(out.target_abi_version == "2.4");
    TEST_ASSERT(out.resource_limits.max_memory_bytes == 64 * 1024 * 1024);
    TEST_ASSERT(out.resource_limits.max_instruction_budget == 50000000ULL);
    TEST_ASSERT(out.resource_limits.max_open_handles == 128);
    TEST_ASSERT(out.supported_doc_types.size() == 3);
    TEST_ASSERT(out.supported_doc_types[0] == "text/plain");
    TEST_ASSERT(out.entry_symbol == "_start");
    TEST_ASSERT(out.entry_stack_size == 131072);
    TEST_ASSERT(out.category == "Developer");
    TEST_ASSERT(out.author == "WinIOS Team");

    std::cout << "[PASS] test_manifest_v3_serialization\n";
}

static void test_package_v3_pack_unpack_valid() {
    AppPackage pkg{};
    pkg.manifest.format_version = "3.0";
    pkg.manifest.bundle_id = "com.winios.sample";
    pkg.manifest.name = "Sample";
    pkg.manifest.display_name = "Sample App";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/sample";
    pkg.manifest.permissions = {"SettingsRead", "WindowCreate"};

    pkg.add_file("bin/sample", {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03});
    pkg.add_file("res/config.json", {'{', '}', '\n'});

    auto packed = pkg.pack_v3();
    TEST_ASSERT(!packed.empty());
    TEST_ASSERT(packed.size() >= 48);
    TEST_ASSERT(std::memcmp(packed.data(), "WIOSPKG3", 8) == 0);

    AppPackage unpacked{};
    auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &unpacked);
    TEST_ASSERT(res == PackageValidationResult::Valid);
    TEST_ASSERT(unpacked.manifest.bundle_id == "com.winios.sample");
    TEST_ASSERT(unpacked.manifest.display_name == "Sample App");
    TEST_ASSERT(unpacked.manifest.files.size() == 2);
    TEST_ASSERT(unpacked.manifest.files[0].path == "bin/sample");
    TEST_ASSERT(unpacked.manifest.files[0].data.size() == 7);
    TEST_ASSERT(unpacked.manifest.files[0].data[0] == 0xDE);

    std::cout << "[PASS] test_package_v3_pack_unpack_valid\n";
}

static void test_package_v3_checksum_mismatch() {
    AppPackage pkg{};
    pkg.manifest.format_version = "3.0";
    pkg.manifest.bundle_id = "com.winios.sample";
    pkg.manifest.name = "Sample";
    pkg.manifest.executable_name = "bin/sample";
    pkg.add_file("bin/sample", {0x01, 0x02, 0x03, 0x04});

    auto packed = pkg.pack_v3();

    // Tamper with file payload data (at the end of the packed buffer)
    packed.back() ^= 0xFF;

    AppPackage unpacked{};
    auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &unpacked);
    TEST_ASSERT(res == PackageValidationResult::ChecksumMismatch);

    std::cout << "[PASS] test_package_v3_checksum_mismatch\n";
}

static void test_package_v3_overlapping_ranges() {
    AppPackage pkg{};
    pkg.manifest.format_version = "3.0";
    pkg.manifest.bundle_id = "com.winios.sample";
    pkg.manifest.name = "Sample";
    pkg.manifest.executable_name = "bin/sample";
    pkg.add_file("bin/sample", {0x01, 0x02, 0x03, 0x04});
    pkg.add_file("res/data.bin", {0x05, 0x06, 0x07, 0x08});

    auto packed = pkg.pack_v3();

    // Manually corrupt second entry's data offset to overlap with first entry's offset
    // In pack_v3():
    // Dir table starts after manifest. Let's find second entry and point its offset to first entry offset
    // Find "res/data.bin"
    const std::string needle = "res/data.bin";
    auto it = std::search(packed.begin(), packed.end(), needle.begin(), needle.end());
    TEST_ASSERT(it != packed.end());
    std::size_t needle_pos = std::distance(packed.begin(), it);
    std::size_t offset_loc = needle_pos + needle.size() + 4 + 4; // path + type + flags

    // Set offset to 0 (which overlaps with header [0..48))
    std::uint64_t bad_offset = 0;
    std::memcpy(packed.data() + offset_loc, &bad_offset, sizeof(bad_offset));

    AppPackage unpacked{};
    auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &unpacked);
    TEST_ASSERT(res == PackageValidationResult::OverlappingEntries);

    std::cout << "[PASS] test_package_v3_overlapping_ranges\n";
}

static void test_package_v3_path_traversal() {
    AppPackage pkg{};
    pkg.manifest.format_version = "3.0";
    pkg.manifest.bundle_id = "com.winios.sample";
    pkg.manifest.name = "Sample";
    pkg.manifest.executable_name = "bin/sample";
    pkg.add_file("../secret.txt", {0x01});

    auto packed = pkg.pack_v3();
    AppPackage unpacked{};
    auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &unpacked);
    TEST_ASSERT(res == PackageValidationResult::PathTraversalAttempt);

    std::cout << "[PASS] test_package_v3_path_traversal\n";
}

static void test_backward_compatibility_v2() {
    AppPackage pkg{};
    pkg.manifest.format_version = "2.0";
    pkg.manifest.bundle_id = "com.winios.legacy";
    pkg.manifest.name = "LegacyApp";
    pkg.manifest.executable_name = "bin/legacy";
    pkg.add_file("bin/legacy", {0xAA, 0xBB, 0xCC});

    auto packed = pkg.pack();
    TEST_ASSERT(std::memcmp(packed.data(), "WIOSPKG2", 8) == 0);

    AppPackage unpacked{};
    auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &unpacked);
    TEST_ASSERT(res == PackageValidationResult::Valid);
    TEST_ASSERT(unpacked.manifest.bundle_id == "com.winios.legacy");
    TEST_ASSERT(unpacked.manifest.files[0].path == "bin/legacy");

    std::cout << "[PASS] test_backward_compatibility_v2\n";
}

static void test_package_trust_evaluation() {
    AppPackage pkg{};
    pkg.manifest.format_version = "3.0";
    pkg.manifest.bundle_id = "com.winios.trusttest";
    pkg.manifest.name = "TrustTest";
    pkg.manifest.executable_name = "bin/test";
    pkg.add_file("bin/test", {0x01, 0x02});

    // Unsigned developer package with developer trust allowed
    auto trust_dev = AppPackage::evaluate_trust(pkg, true);
    TEST_ASSERT(trust_dev.integrity_verified);
    TEST_ASSERT(trust_dev.trust_level == PackageTrustLevel::DeveloperLocal);

    // Unsigned developer package with developer trust disallowed
    auto trust_prod = AppPackage::evaluate_trust(pkg, false);
    TEST_ASSERT(trust_prod.trust_level == PackageTrustLevel::Unsigned);

    // Platform signed package
    pkg.manifest.signature_sha256 = "PLATFORM_ROOT_A16_TRUSTED";
    auto trust_platform = AppPackage::evaluate_trust(pkg, false);
    TEST_ASSERT(trust_platform.trust_level == PackageTrustLevel::PlatformSigned);

    std::cout << "[PASS] test_package_trust_evaluation\n";
}

int main() {
    std::cout << "=== Running Stage 230.4 Package V3 Tests ===" << std::endl;
    test_manifest_v3_serialization();
    test_package_v3_pack_unpack_valid();
    test_package_v3_checksum_mismatch();
    test_package_v3_overlapping_ranges();
    test_package_v3_path_traversal();
    test_backward_compatibility_v2();
    test_package_trust_evaluation();
    std::cout << "=== All Stage 230.4 Tests Passed Successfully ===" << std::endl;
    return 0;
}
