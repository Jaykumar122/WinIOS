#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "fs/apfs_parser.h"

#define ROOTFS_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::fs;

namespace {

// -----------------------------------------------------------------------------
// Test 1: APFS Root Volume Discovery by Role & Identity
// -----------------------------------------------------------------------------
bool test_root_volume_discovery() {
    std::cout << "[RUN] Test 1: Root Volume Discovery by Role & Identity ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container("17.4", "21E219");
    ApfsParser parser;
    std::string err;
    ROOTFS_TEST_ASSERT(parser.load_container(img.data(), img.size(), err), "Load container must succeed");

    ApfsVolumeDiscovery discovery(parser);
    auto root_vol = discovery.discover_root_volume();
    ROOTFS_TEST_ASSERT(root_vol.has_value(), "Root volume must be discovered");
    ROOTFS_TEST_ASSERT(root_vol->role == ApfsVolumeRole::System, "Discovered volume role must be System (0x0001)");
    ROOTFS_TEST_ASSERT(root_vol->name == "iPhone OS", "Discovered volume name must be iPhone OS");
    ROOTFS_TEST_ASSERT(root_vol->index == 0, "Root volume index must be 0");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Ancillary Volume Discovery (User Data & Preboot)
// -----------------------------------------------------------------------------
bool test_ancillary_volume_discovery() {
    std::cout << "[RUN] Test 2: Ancillary Volume Discovery (Data & Preboot) ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    ApfsVolumeDiscovery discovery(parser);

    auto data_vol = discovery.discover_data_volume();
    ROOTFS_TEST_ASSERT(data_vol.has_value(), "Data volume must be discovered");
    ROOTFS_TEST_ASSERT(data_vol->name == "Data", "Data volume name must be Data");
    ROOTFS_TEST_ASSERT(data_vol->index == 1, "Data volume index must be 1");

    auto preboot_vol = discovery.discover_preboot_volume();
    ROOTFS_TEST_ASSERT(preboot_vol.has_value(), "Preboot volume must be discovered");
    ROOTFS_TEST_ASSERT(preboot_vol->name == "Preboot", "Preboot volume name must be Preboot");
    ROOTFS_TEST_ASSERT(preboot_vol->index == 2, "Preboot volume index must be 2");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Sealed System Volume (SSV) Validation
// -----------------------------------------------------------------------------
bool test_ssv_validation() {
    std::cout << "[RUN] Test 3: Sealed System Volume (SSV) Validation ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container("17.4", "21E219");
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    ApfsVolumeDiscovery discovery(parser);
    auto res = discovery.validate_root_filesystem(0);

    ROOTFS_TEST_ASSERT(res.valid, "Root filesystem validation must succeed");
    ROOTFS_TEST_ASSERT(res.is_ssv, "Root volume must be classified as SSV");
    ROOTFS_TEST_ASSERT(res.volume_name == "iPhone OS", "Volume name must match");
    ROOTFS_TEST_ASSERT(res.validation_errors.empty(), "There must be zero validation errors");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Essential Directory Hierarchy Verification
// -----------------------------------------------------------------------------
bool test_essential_directories_verification() {
    std::cout << "[RUN] Test 4: Essential Directory Hierarchy Verification ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    ApfsVolumeDiscovery discovery(parser);
    auto res = discovery.validate_root_filesystem(0);

    const std::vector<std::string> required_dirs = {
        "/sbin", "/System", "/System/Library", "/usr", "/private", "/Library", "/Applications"
    };

    for (const auto& req : required_dirs) {
        bool found = false;
        for (const auto& discovered : res.discovered_directories) {
            if (discovered == req) {
                found = true;
                break;
            }
        }
        ROOTFS_TEST_ASSERT(found, ("Essential directory " + req + " must be present in rootfs").c_str());
    }

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Critical /sbin/launchd Binary Discovery & Conformance
// -----------------------------------------------------------------------------
bool test_launchd_binary_discovery() {
    std::cout << "[RUN] Test 5: Critical /sbin/launchd Binary Discovery ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    ApfsVolumeDiscovery discovery(parser);
    auto res = discovery.validate_root_filesystem(0);

    ROOTFS_TEST_ASSERT(res.launchd_present, "/sbin/launchd must be reported present");
    ROOTFS_TEST_ASSERT(res.launchd_path == "/sbin/launchd", "Launchd path must be /sbin/launchd");
    ROOTFS_TEST_ASSERT(res.launchd_size == 64, "Launchd size must match generated header");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: iOS System Version & Build Train Extraction
// -----------------------------------------------------------------------------
bool test_ios_version_and_build_train() {
    std::cout << "[RUN] Test 6: iOS System Version & Build Train Extraction ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container("17.4", "21E219");
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    ApfsVolumeDiscovery discovery(parser);
    auto res = discovery.validate_root_filesystem(0);

    ROOTFS_TEST_ASSERT(res.system_version_present, "SystemVersion.plist must be present");
    ROOTFS_TEST_ASSERT(res.product_version == "17.4", "Product version must be extracted as 17.4");
    ROOTFS_TEST_ASSERT(res.build_version == "21E219", "Build version must be extracted as 21E219");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Rejection of Ineligible Non-Root Volumes
// -----------------------------------------------------------------------------
bool test_rejection_of_non_root_volumes() {
    std::cout << "[RUN] Test 7: Rejection of Ineligible Non-Root Volumes ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    ApfsVolumeDiscovery discovery(parser);

    // Validate Data volume (Index 1) as root
    auto data_res = discovery.validate_root_filesystem(1);
    ROOTFS_TEST_ASSERT(!data_res.valid, "Data volume must NOT qualify as valid rootfs");
    ROOTFS_TEST_ASSERT(!data_res.launchd_present, "Data volume must lack /sbin/launchd");

    // Validate Preboot volume (Index 2) as root
    auto preboot_res = discovery.validate_root_filesystem(2);
    ROOTFS_TEST_ASSERT(!preboot_res.valid, "Preboot volume must NOT qualify as valid rootfs");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Rejection of Corrupted / Missing /sbin/launchd
// -----------------------------------------------------------------------------
bool test_rejection_of_corrupted_launchd() {
    std::cout << "[RUN] Test 8: Rejection of Corrupted /sbin/launchd ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    // Corrupt Mach-O magic at Block 5 (offset 5 * 4096 = 20480)
    img[5 * 4096 + 0] = 0xAA;
    img[5 * 4096 + 1] = 0xBB;
    img[5 * 4096 + 2] = 0xCC;
    img[5 * 4096 + 3] = 0xDD;

    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    ApfsVolumeDiscovery discovery(parser);
    auto res = discovery.validate_root_filesystem(0);

    ROOTFS_TEST_ASSERT(!res.valid, "Rootfs with corrupted launchd binary must be rejected");
    ROOTFS_TEST_ASSERT(!res.launchd_present, "Launchd must be flagged as non-conforming");
    ROOTFS_TEST_ASSERT(!res.validation_errors.empty(), "Validation error must be recorded for corrupted launchd");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: Rejection of Missing SystemVersion.plist
// -----------------------------------------------------------------------------
bool test_rejection_of_missing_plist() {
    std::cout << "[RUN] Test 9: Rejection of Missing SystemVersion.plist ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    // Overwrite Block 6 with zeroes to clear plist
    std::fill(img.begin() + 6 * 4096, img.begin() + 7 * 4096, std::uint8_t{0});

    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    ApfsVolumeDiscovery discovery(parser);
    auto res = discovery.validate_root_filesystem(0);

    ROOTFS_TEST_ASSERT(!res.valid || res.product_version.empty(),
                       "Rootfs without valid SystemVersion.plist must fail or report empty version");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Discovery Audit Report Generation
// -----------------------------------------------------------------------------
bool test_discovery_audit_report() {
    std::cout << "[RUN] Test 10: Discovery Audit Report Generation ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container("17.4", "21E219");
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    ApfsVolumeDiscovery discovery(parser);
    auto res = discovery.validate_root_filesystem(0);
    auto report = discovery.generate_discovery_report(res);

    ROOTFS_TEST_ASSERT(!report.empty(), "Discovery report must not be empty");
    ROOTFS_TEST_ASSERT(report.find("VALIDATED FOR BOOT HANDOFF") != std::string::npos, "Report must indicate validation");
    ROOTFS_TEST_ASSERT(report.find("iPhone OS") != std::string::npos, "Report must mention volume name");
    ROOTFS_TEST_ASSERT(report.find("17.4") != std::string::npos, "Report must state version 17.4");
    ROOTFS_TEST_ASSERT(report.find("21E219") != std::string::npos, "Report must state build 21E219");
    ROOTFS_TEST_ASSERT(report.find("/sbin/launchd") != std::string::npos, "Report must mention /sbin/launchd");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 117: iOS Root Filesystem Discovery & Validation Suite    \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_root_volume_discovery()) passed++;
    if (test_ancillary_volume_discovery()) passed++;
    if (test_ssv_validation()) passed++;
    if (test_essential_directories_verification()) passed++;
    if (test_launchd_binary_discovery()) passed++;
    if (test_ios_version_and_build_train()) passed++;
    if (test_rejection_of_non_root_volumes()) passed++;
    if (test_rejection_of_corrupted_launchd()) passed++;
    if (test_rejection_of_missing_plist()) passed++;
    if (test_discovery_audit_report()) passed++;

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << passed << " / " << total << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
