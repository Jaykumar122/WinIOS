// ============================================================================
// Stage 238.15: Filesystem Confinement & Handle Safety Adversarial Suite
// ============================================================================
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "tests/support/test_assert.h"
#include "packaging/canonical_path_validator.h"

using namespace winios::packaging;

namespace {

void test_dos_device_names_rejected() {
    std::cout << "[Test 1] Windows Reserved Device Names Rejection...\n";
    const std::string bad_devices[] = {
        "CON", "con.txt", "PRN", "prn.dat", "AUX", "aux.bin", "NUL", "nul.log",
        "COM1", "com1.txt", "COM9", "LPT1", "lpt9.txt", "sub/CON/file.txt"
    };

    std::string norm;
    for (const auto& dev : bad_devices) {
        auto st = CanonicalPathValidator::validate_and_normalize_bundle_path(dev, norm);
        TEST_ASSERT(st == PathValidationStatus::WindowsReservedDevice,
                    ("Device name must be rejected: " + dev).c_str());
    }
    std::cout << "  -> Passed DOS device names rejection\n";
}

void test_alternate_data_streams_rejected() {
    std::cout << "[Test 2] Alternate Data Streams (ADS) Rejection...\n";
    const std::string ads_paths[] = {
        "file.txt:hidden", "payload.bin:$DATA", "dir:stream:extra", "a:b"
    };

    std::string norm;
    for (const auto& p : ads_paths) {
        auto st = CanonicalPathValidator::validate_and_normalize_bundle_path(p, norm);
        TEST_ASSERT(st == PathValidationStatus::AlternateDataStreamForbidden ||
                    st == PathValidationStatus::DriveLetterForbidden,
                    ("ADS must be rejected: " + p).c_str());
    }
    std::cout << "  -> Passed ADS rejection\n";
}

void test_trailing_dot_and_space_rejected() {
    std::cout << "[Test 3] Trailing Dot and Space Normalization Attacks...\n";
    const std::string dot_space_paths[] = {
        "file.txt.", "folder. /file", "test ", "sub/bad ./file"
    };

    std::string norm;
    for (const auto& p : dot_space_paths) {
        auto st = CanonicalPathValidator::validate_and_normalize_bundle_path(p, norm);
        TEST_ASSERT(st == PathValidationStatus::TrailingDotOrSpace,
                    ("Trailing dot/space must be rejected: " + p).c_str());
    }
    std::cout << "  -> Passed trailing dot/space rejection\n";
}

void test_traversal_and_escapes_rejected() {
    std::cout << "[Test 4] Path Traversal and Sandbox Root Escapes...\n";
    const std::string escape_paths[] = {
        "../escape.txt", "a/../../b", "a/b/../../../escape", "/absolute/path", "C:/Windows/System32"
    };

    std::string norm;
    for (const auto& p : escape_paths) {
        auto st = CanonicalPathValidator::validate_and_normalize_bundle_path(p, norm);
        TEST_ASSERT(st == PathValidationStatus::DirectoryTraversal ||
                    st == PathValidationStatus::AbsoluteRootForbidden ||
                    st == PathValidationStatus::DriveLetterForbidden,
                    ("Escape must be rejected: " + p).c_str());
    }
    std::cout << "  -> Passed directory traversal rejection\n";
}

void test_short_name_aliases_rejected() {
    std::cout << "[Test 5] 8.3 Short Name Aliasing Attacks (PROGRA~1)...\n";
    const std::string short_names[] = {
        "PROGRA~1", "app/TEST~1.EXE", "DOCUME~1/secret"
    };

    std::string norm;
    for (const auto& sn : short_names) {
        auto st = CanonicalPathValidator::validate_and_normalize_bundle_path(sn, norm);
        TEST_ASSERT(st == PathValidationStatus::ShortNameForbidden,
                    ("Short name alias must be rejected: " + sn).c_str());
    }
    std::cout << "  -> Passed short name aliases rejection\n";
}

void test_valid_canonical_paths_accepted() {
    std::cout << "[Test 6] Valid Bundle Relative Paths Accepted & Normalized...\n";
    const std::string valid_paths[] = {
        "bin/executable", "assets/images/logo.png", "resources/strings.json", "data/db.sqlite"
    };

    std::string norm;
    for (const auto& vp : valid_paths) {
        auto st = CanonicalPathValidator::validate_and_normalize_bundle_path(vp, norm);
        TEST_ASSERT(st == PathValidationStatus::Valid,
                    ("Valid path must be accepted: " + vp).c_str());
        TEST_ASSERT(norm == vp);
    }
    std::cout << "  -> Passed valid canonical paths acceptance\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " Stage 238.15: Filesystem Confinement Adversarial Suite \n";
    std::cout << "========================================================\n";

    test_dos_device_names_rejected();
    test_alternate_data_streams_rejected();
    test_trailing_dot_and_space_rejected();
    test_traversal_and_escapes_rejected();
    test_short_name_aliases_rejected();
    test_valid_canonical_paths_accepted();

    std::cout << "\nALL STAGE 238.15 TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
