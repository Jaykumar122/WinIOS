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

void test_package_v2_manifest_serialization() {
    std::cout << "[Test 1] Package Format v2 Manifest Serialization & Deserialization...\n";

    PackageManifest m{};
    m.format_version = "2.0";
    m.bundle_id = "com.winios.calc";
    m.name = "Calculator";
    m.version = "2.1.0";
    m.min_os_version = "221.0";
    m.architecture = "arm64";
    m.min_abi_version = "1.5";
    m.executable_name = "bin/calc";
    m.storage_quota_bytes = 20 * 1024 * 1024;
    m.signature_sha256 = "0123456789abcdef0123456789abcdef";
    m.permissions = { "display.window", "storage.private" };

    FileEntry fe{};
    fe.path = "bin/calc";
    fe.size = 100;
    fe.data = std::vector<uint8_t>(100, 0x11);
    m.files.push_back(fe);

    TEST_ASSERT(m.is_valid());

    std::string text = m.serialize();
    PackageManifest m_parsed{};
    TEST_ASSERT(PackageManifest::deserialize(text, m_parsed));

    TEST_ASSERT(m_parsed.format_version == "2.0");
    TEST_ASSERT(m_parsed.bundle_id == "com.winios.calc");
    TEST_ASSERT(m_parsed.name == "Calculator");
    TEST_ASSERT(m_parsed.architecture == "arm64");
    TEST_ASSERT(m_parsed.storage_quota_bytes == 20 * 1024 * 1024);
    TEST_ASSERT(m_parsed.permissions.size() == 2);

    std::cout << "[PASS] Manifest v2 serialization passed.\n";
}

void test_package_v2_packing_and_backward_compatibility() {
    std::cout << "[Test 2] Package v2 Packing & v1 Backward Compatibility...\n";

    AppPackage pkg{};
    pkg.manifest.format_version = "2.0";
    pkg.manifest.bundle_id = "com.winios.test";
    pkg.manifest.name = "Test App";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/test";

    std::vector<uint8_t> dummy_bin = { 0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01 };
    pkg.add_file("bin/test", dummy_bin);

    std::vector<uint8_t> packed = pkg.pack();
    TEST_ASSERT(packed.size() > 16);
    // Magic check: must be WIOSPKG2
    TEST_ASSERT(std::memcmp(packed.data(), "WIOSPKG2", 8) == 0);

    // Unpack v2
    AppPackage unpacked_v2{};
    TEST_ASSERT(AppPackage::unpack(packed.data(), packed.size(), unpacked_v2));
    TEST_ASSERT(unpacked_v2.manifest.bundle_id == "com.winios.test");
    TEST_ASSERT(unpacked_v2.manifest.files.size() == 1);
    TEST_ASSERT(unpacked_v2.manifest.files[0].path == "bin/test");

    // Test Backward Compatibility: simulate WIOSPKG1 container
    std::vector<uint8_t> packed_v1 = packed;
    std::memcpy(packed_v1.data(), "WIOSPKG1", 8);

    AppPackage unpacked_v1{};
    TEST_ASSERT(AppPackage::unpack(packed_v1.data(), packed_v1.size(), unpacked_v1));
    TEST_ASSERT(unpacked_v1.manifest.bundle_id == "com.winios.test");

    std::cout << "[PASS] Packing and backward compatibility passed.\n";
}

void test_package_manifest_security_rejections() {
    std::cout << "[Test 3] Manifest Security Validation Rejections...\n";

    PackageManifest m{};
    m.bundle_id = "com.winios.app";
    m.name = "App";
    m.version = "1.0.0";
    m.executable_name = "bin/app";

    FileEntry fe{};
    fe.path = "bin/app";
    m.files.push_back(fe);

    TEST_ASSERT(m.is_valid());

    // Path traversal in bundle_id
    m.bundle_id = "../evil_escape";
    TEST_ASSERT(!m.is_valid());
    m.bundle_id = "com.winios.app";

    // Path traversal in file entry
    m.files[0].path = "../../../etc/shadow";
    TEST_ASSERT(!m.is_valid());
    m.files[0].path = "bin/app";

    // Invalid architecture
    m.architecture = "x86_64"; // Must be arm64
    TEST_ASSERT(!m.is_valid());

    std::cout << "[PASS] Manifest security validation passed.\n";
}

int main() {
    std::cout << "=== WinIOS Stage 221.16: Package Format v2 Specification Tests ===\n";
    test_package_v2_manifest_serialization();
    test_package_v2_packing_and_backward_compatibility();
    test_package_manifest_security_rejections();
    std::cout << "[ALL PASS] Stage 221.16 tests passed successfully.\n";
    return 0;
}
