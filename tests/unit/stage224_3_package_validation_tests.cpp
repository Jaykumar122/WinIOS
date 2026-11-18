#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "packaging/app_package.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::packaging;

void test_valid_manifest_and_packaging() {
    std::cout << "[Test 1] Valid Package Validation & Packing...\n";

    AppPackage pkg;
    pkg.manifest.format_version = "2.1";
    pkg.manifest.bundle_id = "com.winios.demo";
    pkg.manifest.name = "Demo App";
    pkg.manifest.version = "1.2.3";
    pkg.manifest.architecture = "arm64";
    pkg.manifest.executable_name = "bin/demo";
    pkg.manifest.permissions = { "display.window", "config" };
    pkg.manifest.optional_permissions = { "clipboard.read" };
    pkg.manifest.icon_path = "res/icon.png";
    pkg.manifest.startup_config = "theme=dark";

    pkg.add_file("bin/demo", { 0xCF, 0xFA, 0xED, 0xFE, 0x11, 0x22, 0x33, 0x44 });
    pkg.add_file("res/icon.png", { 0x89, 0x50, 0x4E, 0x47 });

    TEST_ASSERT(pkg.manifest.is_valid());

    auto packed = pkg.pack();
    TEST_ASSERT(!packed.empty());

    AppPackage unpacked;
    auto res = AppPackage::validate_package_data(packed.data(), packed.size(), &unpacked);
    TEST_ASSERT(res == PackageValidationResult::Valid);
    TEST_ASSERT(unpacked.manifest.bundle_id == "com.winios.demo");
    TEST_ASSERT(unpacked.manifest.optional_permissions.size() == 1);
    TEST_ASSERT(unpacked.manifest.icon_path == "res/icon.png");
    TEST_ASSERT(unpacked.manifest.files.size() == 2);

    std::cout << "[PASS] Valid packaging and deserialization verified.\n";
}

void test_corrupted_header_and_magic() {
    std::cout << "[Test 2] Corrupted Header and Magic Validation...\n";

    AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.test";
    pkg.manifest.name = "Test";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/app";
    pkg.add_file("bin/app", { 0x01, 0x02, 0x03 });
    auto packed = pkg.pack();

    // Corrupt magic
    packed[0] = 'B'; packed[1] = 'A'; packed[2] = 'D';
    auto res = AppPackage::validate_package_data(packed.data(), packed.size());
    TEST_ASSERT(res == PackageValidationResult::InvalidMagic);

    // Truncated buffer (< header size)
    res = AppPackage::validate_package_data(packed.data(), 8);
    TEST_ASSERT(res == PackageValidationResult::PayloadTruncated);

    std::cout << "[PASS] Corrupted header and magic rejection verified.\n";
}

void test_security_traversal_and_duplicates() {
    std::cout << "[Test 3] Path Traversal and Duplicate Entries Rejection...\n";

    // 1. Path traversal in file
    AppPackage pkg1;
    pkg1.manifest.bundle_id = "com.winios.traversal";
    pkg1.manifest.name = "Bad App";
    pkg1.manifest.version = "1.0.0";
    pkg1.manifest.executable_name = "bin/app";
    pkg1.add_file("bin/app", { 0x01, 0x02 });
    pkg1.add_file("../../../evil.sh", { 0xAA, 0xBB });
    TEST_ASSERT(!pkg1.manifest.is_valid());

    // 2. Duplicate file paths
    AppPackage pkg2;
    pkg2.manifest.bundle_id = "com.winios.dup";
    pkg2.manifest.name = "Dup App";
    pkg2.manifest.version = "1.0.0";
    pkg2.manifest.executable_name = "bin/app";
    pkg2.add_file("bin/app", { 0x01, 0x02 });
    pkg2.add_file("bin/app", { 0x03, 0x04 }); // Duplicate!

    auto packed_dup = pkg2.pack();
    auto res = AppPackage::validate_package_data(packed_dup.data(), packed_dup.size());
    TEST_ASSERT(res == PackageValidationResult::DuplicateFileEntry);

    std::cout << "[PASS] Path traversal and duplicate entries rejection verified.\n";
}

void test_unsupported_architecture() {
    std::cout << "[Test 4] Unsupported Architecture Rejection...\n";

    AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.x86";
    pkg.manifest.name = "x86 App";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.architecture = "x86_64";
    pkg.manifest.executable_name = "bin/app";
    pkg.add_file("bin/app", { 0x01, 0x02 });

    auto packed = pkg.pack();
    auto res = AppPackage::validate_package_data(packed.data(), packed.size());
    TEST_ASSERT(res == PackageValidationResult::UnsupportedArchitecture);

    std::cout << "[PASS] Unsupported architecture rejection verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 224.3: PACKAGE VALIDATION TESTS\n";
    std::cout << "========================================\n";

    test_valid_manifest_and_packaging();
    test_corrupted_header_and_magic();
    test_security_traversal_and_duplicates();
    test_unsupported_architecture();

    std::cout << "\nALL STAGE 224.3 TESTS PASSED (100%).\n";
    return 0;
}
