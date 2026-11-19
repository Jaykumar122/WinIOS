#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>

#include "packaging/app_package.h"
#include "security/publisher_trust_store.h"
#include "security/developer_keystore.h"

using namespace winios::packaging;
using namespace winios::security;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        std::abort(); \
    } \
} while (0)

static AppPackage create_base_pkg() {
    AppPackage pkg;
    pkg.manifest.format_version = "3.0";
    pkg.manifest.bundle_id = "com.canonical.testapp";
    pkg.manifest.name = "CanonicalTestApp";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.min_os_version = "1.0.0";
    pkg.manifest.architecture = "arm64";
    pkg.manifest.min_abi_version = 1;
    pkg.manifest.target_abi_version = 1;
    pkg.manifest.executable_name = "testapp.bin";
    pkg.manifest.storage_quota_bytes = 10485760;
    pkg.manifest.resource_limits.max_memory_bytes = 67108864;
    pkg.manifest.resource_limits.max_instruction_budget = 10000000;
    pkg.manifest.resource_limits.max_open_handles = 32;
    pkg.manifest.entry_symbol = "_main";
    pkg.manifest.entry_stack_size = 65536;
    return pkg;
}

static void test_deterministic_byte_stream_reproducibility() {
    auto pkg1 = create_base_pkg();
    pkg1.add_file("testapp.bin", {0x7F, 'E', 'L', 'F', 1, 2, 3, 4});
    pkg1.add_file("config.json", {'{', '}', '\n'});
    pkg1.add_file("assets/icon.png", {10, 20, 30, 40});

    auto pkg2 = create_base_pkg();
    pkg2.add_file("testapp.bin", {0x7F, 'E', 'L', 'F', 1, 2, 3, 4});
    pkg2.add_file("config.json", {'{', '}', '\n'});
    pkg2.add_file("assets/icon.png", {10, 20, 30, 40});

    std::vector<uint8_t> bytes1 = pkg1.pack_v3();
    std::vector<uint8_t> bytes2 = pkg2.pack_v3();

    TEST_ASSERT(bytes1.size() == bytes2.size());
    TEST_ASSERT(bytes1 == bytes2);
    TEST_ASSERT(std::memcmp(bytes1.data(), bytes2.data(), bytes1.size()) == 0);

    std::cout << "[PASS] test_deterministic_byte_stream_reproducibility\n";
}

static void test_entry_order_independence() {
    auto pkg1 = create_base_pkg();
    pkg1.manifest.permissions = {"audio.out", "network.none", "storage.read"};
    pkg1.add_file("zebra.txt", {'Z'});
    pkg1.add_file("apple.txt", {'A'});
    pkg1.add_file("testapp.bin", {1, 2, 3});

    auto pkg2 = create_base_pkg();
    // Add in reverse order
    pkg2.manifest.permissions = {"storage.read", "audio.out", "network.none"};
    pkg2.add_file("testapp.bin", {1, 2, 3});
    pkg2.add_file("apple.txt", {'A'});
    pkg2.add_file("zebra.txt", {'Z'});

    std::vector<uint8_t> bytes1 = pkg1.pack_v3();
    std::vector<uint8_t> bytes2 = pkg2.pack_v3();

    TEST_ASSERT(bytes1.size() == bytes2.size());
    TEST_ASSERT(bytes1 == bytes2);

    std::cout << "[PASS] test_entry_order_independence\n";
}

static void test_path_separator_normalization() {
    auto pkg_forward = create_base_pkg();
    pkg_forward.add_file("testapp.bin", {1, 2, 3});
    pkg_forward.add_file("assets/textures/player.bin", {4, 5, 6});

    auto pkg_windows = create_base_pkg();
    pkg_windows.add_file("testapp.bin", {1, 2, 3});
    pkg_windows.add_file("assets\\textures\\player.bin", {4, 5, 6});

    std::vector<uint8_t> bytes_fwd = pkg_forward.pack_v3();
    std::vector<uint8_t> bytes_win = pkg_windows.pack_v3();

    TEST_ASSERT(bytes_fwd == bytes_win);

    std::cout << "[PASS] test_path_separator_normalization\n";
}

static void test_signature_domain_canonical_coverage() {
    auto pkg = create_base_pkg();
    pkg.add_file("testapp.bin", {1, 2, 3, 4, 5, 6, 7, 8});
    pkg.add_file("data.bin", {9, 10, 11, 12});

    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.canonical.testapp", "Canonical Org", "KEY-CANON-01"
    );

    std::vector<uint8_t> pkg_bytes = pkg.pack_v3();
    bool ok_sign = PublisherTrustStore::sign_package_v2(pkg_bytes, dev_id);
    TEST_ASSERT(ok_sign);

    PublisherTrustStore store;
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    auto res = store.verify_package_signature(pkg_bytes.data(), pkg_bytes.size(), TrustPolicy::Development);
    TEST_ASSERT(res.status == SignatureStatus::DeveloperSigned);
    TEST_ASSERT(res.is_trusted);

    // Tamper with canonical padding byte (at offset 48 + 5)
    pkg_bytes[48 + 5] ^= 0x01;
    auto res_tamper = store.verify_package_signature(pkg_bytes.data(), pkg_bytes.size(), TrustPolicy::Development);
    TEST_ASSERT(res_tamper.status == SignatureStatus::CorruptSignature);
    TEST_ASSERT(!res_tamper.is_trusted);

    std::cout << "[PASS] test_signature_domain_canonical_coverage\n";
}

static void test_unpack_roundtrip() {
    auto pkg = create_base_pkg();
    pkg.add_file("testapp.bin", {0xAA, 0xBB, 0xCC});
    pkg.add_file("sub/config.ini", {'k', '=', 'v', '\n'});

    std::vector<uint8_t> bytes = pkg.pack_v3();

    AppPackage unpacked;
    bool ok = AppPackage::unpack(bytes.data(), bytes.size(), unpacked);
    TEST_ASSERT(ok);
    TEST_ASSERT(unpacked.manifest.bundle_id == "com.canonical.testapp");
    TEST_ASSERT(unpacked.manifest.files.size() == 2);

    bool found_bin = false;
    bool found_ini = false;
    for (const auto& f : unpacked.manifest.files) {
        if (f.path == "testapp.bin") {
            found_bin = true;
            TEST_ASSERT(f.data.size() == 3);
            TEST_ASSERT(f.data[0] == 0xAA);
        } else if (f.path == "sub/config.ini") {
            found_ini = true;
            TEST_ASSERT(f.data.size() == 4);
        }
    }
    TEST_ASSERT(found_bin && found_ini);

    std::cout << "[PASS] test_unpack_roundtrip\n";
}

int main() {
    std::cout << "=== WinIOS Stage 232.9 Package Canonicalization & Signature Domain Tests ===\n";
    test_deterministic_byte_stream_reproducibility();
    test_entry_order_independence();
    test_path_separator_normalization();
    test_signature_domain_canonical_coverage();
    test_unpack_roundtrip();
    std::cout << "All Stage 232.9 tests passed successfully!\n";
    return 0;
}
