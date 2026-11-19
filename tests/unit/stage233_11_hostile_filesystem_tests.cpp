#include "packaging/canonical_path_validator.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

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

namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// Test 1: Device Namespace Rejection (\\?\, \\.\, \??\)
// -----------------------------------------------------------------------------
static void test_device_namespaces() {
    std::cout << "[Test 1] Device Namespace Rejection (\\\\?\\, \\\\.\\, \\??\\)...\n";

    std::string out;
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("\\\\?\\C:\\evil", out) ==
                PathValidationStatus::DeviceNamespaceForbidden, "\\\\?\\ rejected");
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("\\\\.\\PhysicalDrive0", out) ==
                PathValidationStatus::DeviceNamespaceForbidden, "\\\\.\\ rejected");
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("\\??\\C:\\System32", out) ==
                PathValidationStatus::DeviceNamespaceForbidden, "\\??\\ rejected");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Windows Reserved Device Names (CON, PRN, AUX, NUL, COM1-9, LPT1-9)
// -----------------------------------------------------------------------------
static void test_reserved_device_names() {
    std::cout << "[Test 2] Windows Reserved Device Names...\n";

    std::vector<std::string> devices = {
        "CON", "con.txt", "PRN", "prn.bin", "AUX", "aux.dat", "NUL", "nul.iso",
        "COM1", "com1.log", "COM9", "com9.sys", "LPT1", "lpt1.prn", "LPT9", "lpt9.out",
        "nested/path/to/CON.txt", "sub/LPT2/file"
    };

    std::string out;
    for (const auto& dev : devices) {
        auto status = CanonicalPathValidator::validate_and_normalize_bundle_path(dev, out);
        TEST_ASSERT(status == PathValidationStatus::WindowsReservedDevice,
                    ("Device rejected: " + dev).c_str());
    }

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Alternate Data Streams (Colons outside drive letters)
// -----------------------------------------------------------------------------
static void test_alternate_data_streams() {
    std::cout << "[Test 3] Alternate Data Streams (ADS)...\n";

    std::vector<std::string> ads_paths = {
        "app.exe:hidden.dll",
        "resources/icon.png:stream",
        "data.txt:$DATA",
        "payload.bin::$INDEX_ALLOCATION"
    };

    std::string out;
    for (const auto& p : ads_paths) {
        auto status = CanonicalPathValidator::validate_and_normalize_bundle_path(p, out);
        TEST_ASSERT(status == PathValidationStatus::AlternateDataStreamForbidden,
                    ("ADS rejected: " + p).c_str());
    }

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: 8.3 Short Name (SFN) Aliases (PROGRA~1, etc.)
// -----------------------------------------------------------------------------
static void test_short_name_aliases() {
    std::cout << "[Test 4] 8.3 Short Name Aliasing (PROGRA~1, etc.)...\n";

    std::vector<std::string> sfn_paths = {
        "PROGRA~1/app.exe",
        "sub/LONGDI~2/file.txt",
        "SHORT~1.BIN",
        "dir/FILE~99.EXT"
    };

    std::string out;
    for (const auto& p : sfn_paths) {
        auto status = CanonicalPathValidator::validate_and_normalize_bundle_path(p, out);
        TEST_ASSERT(status == PathValidationStatus::ShortNameForbidden,
                    ("SFN rejected: " + p).c_str());
    }

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Trailing Dots & Whitespace
// -----------------------------------------------------------------------------
static void test_trailing_dots_and_spaces() {
    std::cout << "[Test 5] Trailing Dots and Spaces...\n";

    std::vector<std::string> bad_paths = {
        "secret.txt.",
        "secret.txt..",
        "folder /sub",
        "file.exe "
    };

    std::string out;
    for (const auto& p : bad_paths) {
        auto status = CanonicalPathValidator::validate_and_normalize_bundle_path(p, out);
        TEST_ASSERT(status == PathValidationStatus::TrailingDotOrSpace,
                    ("Trailing dot/space rejected: " + p).c_str());
    }

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 6: Directory Traversal, Absolute Paths & Drive Letters
// -----------------------------------------------------------------------------
static void test_directory_traversal_and_absolute() {
    std::cout << "[Test 6] Directory Traversal and Absolute Roots...\n";

    std::string out;

    // Traversal
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("../escape.bin", out) ==
                PathValidationStatus::DirectoryTraversal, "Leading .. rejected");
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("dir/../../escape.bin", out) ==
                PathValidationStatus::DirectoryTraversal, "Nested .. traversal rejected");

    // Absolute
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("/bin/app", out) ==
                PathValidationStatus::AbsoluteRootForbidden, "Unix absolute root rejected");
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("\\Windows\\System32", out) ==
                PathValidationStatus::AbsoluteRootForbidden, "Windows absolute root rejected");

    // Drive letter
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("C:/evil.exe", out) ==
                PathValidationStatus::DriveLetterForbidden, "Drive letter C: rejected");
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("D:\\app", out) ==
                PathValidationStatus::DriveLetterForbidden, "Drive letter D: rejected");

    std::cout << "  -> Passed.\n";
}

// -----------------------------------------------------------------------------
// Test 7: Sandbox Physical Boundary Enforcement (is_strictly_inside_root)
// -----------------------------------------------------------------------------
static void test_sandbox_boundary_enforcement() {
    std::cout << "[Test 7] Sandbox Physical Boundary Enforcement...\n";

    fs::path temp_sandbox = fs::temp_directory_path() / "winios_test_sandbox_233";
    fs::create_directories(temp_sandbox / "app_data");

    fs::path valid_target = temp_sandbox / "app_data" / "file.txt";
    fs::path escape_target = fs::temp_directory_path() / "escaped_file.txt";

    // 1. Strictly inside root
    TEST_ASSERT(CanonicalPathValidator::is_strictly_inside_root(temp_sandbox.string(), valid_target.string()),
                "Target inside sandbox permitted");

    // 2. Escaping root
    TEST_ASSERT(!CanonicalPathValidator::is_strictly_inside_root(temp_sandbox.string(), escape_target.string()),
                "Target outside sandbox rejected");

    // 3. Parent traversal attempt via string concatenation
    std::string sneaky = temp_sandbox.string() + "/../escaped.txt";
    TEST_ASSERT(!CanonicalPathValidator::is_strictly_inside_root(temp_sandbox.string(), sneaky),
                "Sneaky traversal out of sandbox rejected");

    fs::remove_all(temp_sandbox);
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

    std::cout << "=== Stage 233.11: Hostile Filesystem Tests ===\n";

    test_device_namespaces();
    test_reserved_device_names();
    test_alternate_data_streams();
    test_short_name_aliases();
    test_trailing_dots_and_spaces();
    test_directory_traversal_and_absolute();
    test_sandbox_boundary_enforcement();

    std::cout << "=== All Stage 233.11 Hostile Filesystem Tests PASSED ===\n";
    return 0;
}
