#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <cstdlib>

#include "packaging/canonical_path_validator.h"

using namespace winios::packaging;
namespace fs = std::filesystem;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        std::exit(1); \
    } \
} while (0)

static void test_windows_device_namespaces() {
    std::string norm;
    auto s1 = CanonicalPathValidator::validate_and_normalize_bundle_path("\\\\?\\C:\\Windows", norm);
    TEST_ASSERT(s1 == PathValidationStatus::DeviceNamespaceForbidden);

    auto s2 = CanonicalPathValidator::validate_and_normalize_bundle_path("//?/D:/Data", norm);
    TEST_ASSERT(s2 == PathValidationStatus::DeviceNamespaceForbidden);

    auto s3 = CanonicalPathValidator::validate_and_normalize_bundle_path("\\\\.\\PhysicalDrive0", norm);
    TEST_ASSERT(s3 == PathValidationStatus::DeviceNamespaceForbidden);

    auto s4 = CanonicalPathValidator::validate_and_normalize_bundle_path("\\??\\C:\\test", norm);
    TEST_ASSERT(s4 == PathValidationStatus::DeviceNamespaceForbidden);

    std::cout << "[PASS] test_windows_device_namespaces\n";
}

static void test_short_name_aliases() {
    std::string norm;
    auto s1 = CanonicalPathValidator::validate_and_normalize_bundle_path("PROGRA~1/app.exe", norm);
    TEST_ASSERT(s1 == PathValidationStatus::ShortNameForbidden);

    auto s2 = CanonicalPathValidator::validate_and_normalize_bundle_path("sub/DATA~12/file.txt", norm);
    TEST_ASSERT(s2 == PathValidationStatus::ShortNameForbidden);

    // Regular tilde not followed by digit should be valid
    auto s3 = CanonicalPathValidator::validate_and_normalize_bundle_path("sub/my~file.txt", norm);
    TEST_ASSERT(s3 == PathValidationStatus::Valid);

    std::cout << "[PASS] test_short_name_aliases\n";
}

static void test_reserved_device_names() {
    std::string norm;
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("CON", norm) == PathValidationStatus::WindowsReservedDevice);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("con.txt", norm) == PathValidationStatus::WindowsReservedDevice);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("sub/PRN.dat", norm) == PathValidationStatus::WindowsReservedDevice);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("AUX", norm) == PathValidationStatus::WindowsReservedDevice);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("nul", norm) == PathValidationStatus::WindowsReservedDevice);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("COM1.log", norm) == PathValidationStatus::WindowsReservedDevice);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("lpt9.bin", norm) == PathValidationStatus::WindowsReservedDevice);

    std::cout << "[PASS] test_reserved_device_names\n";
}

static void test_alternate_data_streams() {
    std::string norm;
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("image.png:stream", norm) == PathValidationStatus::AlternateDataStreamForbidden);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("data.txt::$DATA", norm) == PathValidationStatus::AlternateDataStreamForbidden);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("sub/file.exe:hidden", norm) == PathValidationStatus::AlternateDataStreamForbidden);

    std::cout << "[PASS] test_alternate_data_streams\n";
}

static void test_trailing_dot_or_space() {
    std::string norm;
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("file.txt.", norm) == PathValidationStatus::TrailingDotOrSpace);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("file.txt ", norm) == PathValidationStatus::TrailingDotOrSpace);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("sub. /child.txt", norm) == PathValidationStatus::TrailingDotOrSpace);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("folder. /", norm) == PathValidationStatus::TrailingDotOrSpace);

    std::cout << "[PASS] test_trailing_dot_or_space\n";
}

static void test_directory_traversals() {
    std::string norm;
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("../escape.txt", norm) == PathValidationStatus::DirectoryTraversal);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("sub/../../escape.txt", norm) == PathValidationStatus::DirectoryTraversal);
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("a/b/../../../c", norm) == PathValidationStatus::DirectoryTraversal);

    // Valid internal relative resolution
    TEST_ASSERT(CanonicalPathValidator::validate_and_normalize_bundle_path("a/b/../c/file.txt", norm) == PathValidationStatus::Valid);
    TEST_ASSERT(norm == "a/c/file.txt");

    std::cout << "[PASS] test_directory_traversals\n";
}

static void test_physical_sandbox_confinement() {
    fs::path temp_sandbox = fs::temp_directory_path() / "winios_stage232_11_sandbox";
    fs::path temp_outside = fs::temp_directory_path() / "winios_stage232_11_outside";
    fs::create_directories(temp_sandbox);
    fs::create_directories(temp_outside);

    fs::path file_inside = temp_sandbox / "sub" / "app.txt";
    fs::create_directories(temp_sandbox / "sub");
    {
        std::ofstream ofs(file_inside);
        ofs << "inside";
    }

    fs::path file_outside = temp_outside / "escaped.txt";
    {
        std::ofstream ofs(file_outside);
        ofs << "outside";
    }

    // Inside must return true
    TEST_ASSERT(CanonicalPathValidator::is_strictly_inside_root(temp_sandbox.string(), file_inside.string()));

    // Outside must return false
    TEST_ASSERT(!CanonicalPathValidator::is_strictly_inside_root(temp_sandbox.string(), file_outside.string()));

    // Physical canonical path resolution test
    std::string canon;
    bool ok_res = CanonicalPathValidator::resolve_canonical_physical_path(temp_sandbox.string(), canon);
    TEST_ASSERT(ok_res);
    TEST_ASSERT(!canon.empty());

    // Reparse point test on regular directory -> false
    TEST_ASSERT(!CanonicalPathValidator::is_reparse_point(temp_sandbox.string()));

    fs::remove_all(temp_sandbox);
    fs::remove_all(temp_outside);
    std::cout << "[PASS] test_physical_sandbox_confinement\n";
}

int main() {
    std::cout << "=== WinIOS Stage 232.11 Hardened CanonicalPathValidator Tests ===\n";
    test_windows_device_namespaces();
    test_short_name_aliases();
    test_reserved_device_names();
    test_alternate_data_streams();
    test_trailing_dot_or_space();
    test_directory_traversals();
    test_physical_sandbox_confinement();
    std::cout << "All Stage 232.11 tests passed successfully!\n";
    return 0;
}
