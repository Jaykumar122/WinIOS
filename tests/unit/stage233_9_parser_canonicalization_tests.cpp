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

static AppPackage create_valid_package() {
    AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.parser.test";
    pkg.manifest.name = "ParserTest";
    pkg.manifest.display_name = "Parser Test App";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.architecture = "arm64";
    pkg.manifest.executable_name = "bin/main";

    // Executable
    std::vector<std::uint8_t> exec_data(128, 0x55);
    pkg.add_file("bin/main", exec_data);

    // Asset
    std::string config_txt = "theme=dark\nfont=SFPro\n";
    std::vector<std::uint8_t> cfg_data(config_txt.begin(), config_txt.end());
    pkg.add_file("assets/config.txt", cfg_data);

    return pkg;
}

// -----------------------------------------------------------------------------
// Test 1: Valid Package Pack & Unpack Roundtrip
// -----------------------------------------------------------------------------
static void test_valid_pack_unpack() {
    std::cout << "[Test 1] Valid Package Pack & Unpack Roundtrip...\n";

    auto orig_pkg = create_valid_package();
    auto raw_data = orig_pkg.pack_v3();
    TEST_ASSERT(!raw_data.empty(), "Pack v3 produced data");

    AppPackage parsed_pkg;
    auto res = AppPackage::validate_package_data(raw_data.data(), raw_data.size(), &parsed_pkg);
    TEST_ASSERT(res == PackageValidationResult::Valid, "Validation is Valid");
    TEST_ASSERT(parsed_pkg.manifest.bundle_id == orig_pkg.manifest.bundle_id, "Bundle ID matches");
    TEST_ASSERT(parsed_pkg.manifest.files.size() == 2, "2 files unpacked");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Path Traversal Defenses (.., absolute, drive letters)
// -----------------------------------------------------------------------------
static void test_path_traversal_defense() {
    std::cout << "[Test 2] Path Traversal Defense...\n";

    auto orig_pkg = create_valid_package();
    auto raw_data = orig_pkg.pack_v3();

    // 1. Injected relative traversal: ../evil.bin
    {
        auto bad_data = raw_data;
        // Locate "assets/config.txt" in raw_data and replace with "../evil.txt    "
        std::string target = "assets/config.txt";
        auto pos = bad_data.size();
        for (std::size_t i = 0; i + target.size() <= bad_data.size(); ++i) {
            if (std::memcmp(bad_data.data() + i, target.data(), target.size()) == 0) {
                pos = i;
                break;
            }
        }
        TEST_ASSERT(pos < bad_data.size(), "Found target path in package");
        std::string bad_path = "../evil.txt    ";
        std::memcpy(bad_data.data() + pos, bad_path.data(), bad_path.size());

        auto res = AppPackage::validate_package_data(bad_data.data(), bad_data.size());
        TEST_ASSERT(res == PackageValidationResult::PathTraversalAttempt, "Traversal ../ rejected");
    }

    // 2. Injected absolute unix path: /etc/passwd
    {
        auto bad_data = raw_data;
        std::string target = "assets/config.txt";
        auto pos = bad_data.size();
        for (std::size_t i = 0; i + target.size() <= bad_data.size(); ++i) {
            if (std::memcmp(bad_data.data() + i, target.data(), target.size()) == 0) {
                pos = i;
                break;
            }
        }
        TEST_ASSERT(pos < bad_data.size(), "Found target path");
        std::string bad_path = "/etc/passwd    ";
        std::memcpy(bad_data.data() + pos, bad_path.data(), bad_path.size());

        auto res = AppPackage::validate_package_data(bad_data.data(), bad_data.size());
        TEST_ASSERT(res == PackageValidationResult::PathTraversalAttempt, "Absolute / rejected");
    }

    // 3. Injected Windows drive path: C:\cmd.exe
    {
        auto bad_data = raw_data;
        std::string target = "assets/config.txt";
        auto pos = bad_data.size();
        for (std::size_t i = 0; i + target.size() <= bad_data.size(); ++i) {
            if (std::memcmp(bad_data.data() + i, target.data(), target.size()) == 0) {
                pos = i;
                break;
            }
        }
        TEST_ASSERT(pos < bad_data.size(), "Found target path");
        std::string bad_path = "C:\\Windows\\cmd";
        std::memcpy(bad_data.data() + pos, bad_path.data(), bad_path.size());

        auto res = AppPackage::validate_package_data(bad_data.data(), bad_data.size());
        TEST_ASSERT(res == PackageValidationResult::PathTraversalAttempt, "Drive letter C: rejected");
    }

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Duplicate & Case-Colliding File Entries
// -----------------------------------------------------------------------------
static void test_duplicate_file_entries() {
    std::cout << "[Test 3] Duplicate & Case-Colliding Entries...\n";

    // 1. Exact Duplicate Path
    {
        auto dup_pkg = create_valid_package();
        dup_pkg.add_file("bin/main", {0x11, 0x22, 0x33}); // Duplicate!
        auto dup_data = dup_pkg.pack_v3();

        auto res = AppPackage::validate_package_data(dup_data.data(), dup_data.size());
        TEST_ASSERT(res == PackageValidationResult::DuplicateFileEntry, "Duplicate entry rejected");
    }

    // 2. Case-Insensitive Colliding Path ("BIN/MAIN" vs "bin/main")
    {
        auto case_pkg = create_valid_package();
        case_pkg.add_file("BIN/MAIN", {0x44, 0x55, 0x66}); // Case collision!
        auto case_data = case_pkg.pack_v3();

        auto res = AppPackage::validate_package_data(case_data.data(), case_data.size());
        TEST_ASSERT(res == PackageValidationResult::DuplicateFileEntry, "Case collision rejected");
    }

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Checksum Mismatch Detection
// -----------------------------------------------------------------------------
static void test_checksum_mismatch() {
    std::cout << "[Test 4] File Entry Checksum Mismatch...\n";

    auto orig_pkg = create_valid_package();
    auto raw_data = orig_pkg.pack_v3();

    // Corrupt 1 byte in the file data payload (near end of package)
    raw_data[raw_data.size() - 10] ^= 0x01;

    auto res = AppPackage::validate_package_data(raw_data.data(), raw_data.size());
    TEST_ASSERT(res == PackageValidationResult::ChecksumMismatch, "Checksum mismatch detected");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Truncation & Header Corruption
// -----------------------------------------------------------------------------
static void test_truncation_and_corrupt_header() {
    std::cout << "[Test 5] Truncation & Header Corruption...\n";

    auto orig_pkg = create_valid_package();
    auto raw_data = orig_pkg.pack_v3();

    // 1. Truncated package payload
    {
        auto trunc_data = raw_data;
        trunc_data.resize(trunc_data.size() - 64);
        auto res = AppPackage::validate_package_data(trunc_data.data(), trunc_data.size());
        TEST_ASSERT(res == PackageValidationResult::PayloadTruncated, "Truncated payload detected");
    }

    // 2. Corrupted Magic
    {
        auto bad_magic = raw_data;
        bad_magic[0] = 'N';
        bad_magic[1] = 'O';
        bad_magic[2] = 'P';
        bad_magic[3] = 'E';
        auto res = AppPackage::validate_package_data(bad_magic.data(), bad_magic.size());
        TEST_ASSERT(res == PackageValidationResult::CorruptedHeader || res == PackageValidationResult::InvalidMagic,
                    "Bad magic rejected");
    }

    // 3. Corrupted Version (e.g. version = 99)
    {
        auto bad_ver = raw_data;
        std::uint32_t fake_ver = 99;
        std::memcpy(bad_ver.data() + 8, &fake_ver, 4);
        auto res = AppPackage::validate_package_data(bad_ver.data(), bad_ver.size());
        TEST_ASSERT(res == PackageValidationResult::CorruptedHeader, "Bad version rejected");
    }

    std::cout << "  -> Passed.\n";
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

    std::cout << "=== Stage 233.9: Package Parser & Canonicalization Tests ===\n";

    test_valid_pack_unpack();
    test_path_traversal_defense();
    test_duplicate_file_entries();
    test_checksum_mismatch();
    test_truncation_and_corrupt_header();

    std::cout << "=== All Stage 233.9 Package Parser & Canonicalization Tests PASSED ===\n";
    return 0;
}
