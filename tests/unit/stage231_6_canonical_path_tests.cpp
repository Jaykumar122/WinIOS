#include <iostream>
#include <string>
#include <vector>

#include "packaging/canonical_path_validator.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Canonical Path Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::packaging;

static void test_normal_path_resolution() {
    std::cout << "[Stage 231.6] Testing normal path normalization...\n";

    std::string out;
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("bin/calc", out) == PathValidationStatus::Valid);
    TEST_ASSERT(out == "bin/calc");

    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("res\\icons\\app.png", out) == PathValidationStatus::Valid);
    TEST_ASSERT(out == "res/icons/app.png");

    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("bin/./tools/./util", out) == PathValidationStatus::Valid);
    TEST_ASSERT(out == "bin/tools/util");

    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("bin/nested/../calc", out) == PathValidationStatus::Valid);
    TEST_ASSERT(out == "bin/calc");

    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("a/b/c/../../d", out) == PathValidationStatus::Valid);
    TEST_ASSERT(out == "a/d");
}

static void test_traversal_attacks() {
    std::cout << "[Stage 231.6] Testing directory traversal rejection...\n";

    std::string out;
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("../escape.exe", out) == PathValidationStatus::DirectoryTraversal);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("bin/../../escape.exe", out) == PathValidationStatus::DirectoryTraversal);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("a/b/c/../../../..", out) == PathValidationStatus::DirectoryTraversal);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("..\\escape", out) == PathValidationStatus::DirectoryTraversal);
}

static void test_reserved_device_names() {
    std::cout << "[Stage 231.6] Testing Windows reserved device name rejection...\n";

    std::string out;
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("CON", out) == PathValidationStatus::WindowsReservedDevice);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("con.txt", out) == PathValidationStatus::WindowsReservedDevice);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("bin/PRN", out) == PathValidationStatus::WindowsReservedDevice);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("res/aux.dat", out) == PathValidationStatus::WindowsReservedDevice);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("NUL.png", out) == PathValidationStatus::WindowsReservedDevice);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("bin/COM1.dll", out) == PathValidationStatus::WindowsReservedDevice);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("data/lpt9.log", out) == PathValidationStatus::WindowsReservedDevice);
}

static void test_ads_and_drive_letters() {
    std::cout << "[Stage 231.6] Testing Alternate Data Streams and drive letter rejection...\n";

    std::string out;
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("C:\\Windows\\System32\\calc.exe", out) == PathValidationStatus::DriveLetterForbidden);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("D:file.txt", out) == PathValidationStatus::DriveLetterForbidden);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("file.txt:hidden", out) == PathValidationStatus::AlternateDataStreamForbidden);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("icon.png:$DATA", out) == PathValidationStatus::AlternateDataStreamForbidden);
}

static void test_unc_and_absolute_roots() {
    std::cout << "[Stage 231.6] Testing UNC and absolute root rejection...\n";

    std::string out;
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("\\\\server\\share\\calc.exe", out) == PathValidationStatus::UncPathForbidden);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("//server/share/calc.exe", out) == PathValidationStatus::UncPathForbidden);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("/usr/bin/sh", out) == PathValidationStatus::AbsoluteRootForbidden);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("\\Windows\\System32", out) == PathValidationStatus::AbsoluteRootForbidden);
}

static void test_trailing_dots_spaces_and_control_chars() {
    std::cout << "[Stage 231.6] Testing trailing dots, spaces, and control char rejection...\n";

    std::string out;
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("bin/calc.", out) == PathValidationStatus::TrailingDotOrSpace);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("bin/calc ", out) == PathValidationStatus::TrailingDotOrSpace);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("bin /calc", out) == PathValidationStatus::TrailingDotOrSpace);

    std::string null_str = "bin/calc.exe";
    null_str[4] = '\0';
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path(null_str, out) == PathValidationStatus::NullCharacterForbidden);

    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("bin/\x1Fpoison.exe", out) == PathValidationStatus::ControlCharactersForbidden);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("bin/\tpoison.exe", out) == PathValidationStatus::ControlCharactersForbidden);
}

static void test_depth_and_length_boundaries() {
    std::cout << "[Stage 231.6] Testing path depth and length boundaries...\n";

    std::string out;
    // Exactly 16 levels -> Valid
    std::string depth16 = "1/2/3/4/5/6/7/8/9/10/11/12/13/14/15/16";
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path(depth16, out) == PathValidationStatus::Valid);

    // 17 levels -> DepthExceeded
    std::string depth17 = "1/2/3/4/5/6/7/8/9/10/11/12/13/14/15/16/17";
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path(depth17, out) == PathValidationStatus::DepthExceeded);

    // Excessive length > 256
    std::string long_path(260, 'a');
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path(long_path, out) == PathValidationStatus::PathTooLong);
}

static void test_strictly_inside_root() {
    std::cout << "[Stage 231.6] Testing is_strictly_inside_root...\n";

    std::string root = "D:/WinIOS/apps/com.sample.app";
    TEST_ASSERT(CanonicalPathValidator::is_strictly_inside_root(root, "D:/WinIOS/apps/com.sample.app/bin/calc"));
    TEST_ASSERT(CanonicalPathValidator::is_strictly_inside_root(root, "D:\\WinIOS\\apps\\com.sample.app\\res\\icon.png"));

    // Prefix collision: "/com.sample.app2" should NOT be inside "/com.sample.app"
    TEST_ASSERT(!CanonicalPathValidator::is_strictly_inside_root(root, "D:/WinIOS/apps/com.sample.app2/bin/calc"));

    // Parent escape
    TEST_ASSERT(!CanonicalPathValidator::is_strictly_inside_root(root, "D:/WinIOS/apps/calc.exe"));
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 231.6: Canonical Path Validator Tests       \n";
    std::cout << "====================================================\n";

    test_normal_path_resolution();
    test_traversal_attacks();
    test_reserved_device_names();
    test_ads_and_drive_letters();
    test_unc_and_absolute_roots();
    test_trailing_dots_spaces_and_control_chars();
    test_depth_and_length_boundaries();
    test_strictly_inside_root();

    std::cout << "\n[Stage 231.6] ALL TESTS PASSED SUCCESSFULLY.\n";
    return 0;
}
