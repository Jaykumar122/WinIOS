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

#define APFS_TEST_ASSERT(cond, msg) \
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
// Test 1: Container Superblock (NXSB) Validation & Magic Conformance
// -----------------------------------------------------------------------------
bool test_container_superblock_validation() {
    std::cout << "[RUN] Test 1: Container Superblock (NXSB) Validation ... ";

    APFS_TEST_ASSERT(kApfsContainerMagic == 0x4253584Eu, "NXSB magic must be 0x4253584E");
    APFS_TEST_ASSERT(kApfsVolumeMagic == 0x53465041u, "APFS volume magic must be 0x53465041");

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container("17.4", "21E219");
    APFS_TEST_ASSERT(!img.empty(), "Container image generation must succeed");

    ApfsParser parser;
    std::string err;
    APFS_TEST_ASSERT(parser.load_container(img.data(), img.size(), err), "Valid APFS container must load cleanly");
    APFS_TEST_ASSERT(parser.is_valid_container(), "Parser state must indicate valid container");
    APFS_TEST_ASSERT(parser.block_size() == 4096u, "Block size must be 4096 bytes");
    APFS_TEST_ASSERT(parser.block_count() == 16u, "Block count must be 16");

    // Test rejection of truncated buffer
    ApfsParser bad_parser;
    APFS_TEST_ASSERT(!bad_parser.load_container(img.data(), 2048, err), "Truncated buffer (<4096) must be rejected");

    // Test rejection of corrupted magic (nx_magic at byte offset 32 / 0x20)
    auto corrupted = img;
    corrupted[32] = 'B'; corrupted[33] = 'A'; corrupted[34] = 'D'; corrupted[35] = '!';
    corrupted[0] = 'B'; corrupted[1] = 'A'; corrupted[2] = 'D'; corrupted[3] = '!';
    APFS_TEST_ASSERT(!bad_parser.load_container(corrupted.data(), corrupted.size(), err), "Corrupted magic must be rejected");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Fletcher-64 Checksum Algorithm Conformance
// -----------------------------------------------------------------------------
bool test_fletcher64_checksum() {
    std::cout << "[RUN] Test 2: Fletcher-64 Checksum Conformance ... ";

    // Small or null buffer returns 0
    APFS_TEST_ASSERT(ApfsParser::compute_fletcher64(nullptr, 0) == 0, "Null pointer must return 0");
    std::uint32_t tiny[1] = { 0x12345678u };
    APFS_TEST_ASSERT(ApfsParser::compute_fletcher64(tiny, sizeof(tiny)) == 0, "Buffer < 8 bytes must return 0");

    // Two 64-bit words test buffer
    std::uint32_t buf[6] = {
        0x00000000u, 0x00000000u, // o_cksum field (skipped by algorithm)
        0x11111111u, 0x22222222u,
        0x33333333u, 0x44444444u
    };

    const std::uint64_t ck1 = ApfsParser::compute_fletcher64(buf, sizeof(buf));
    APFS_TEST_ASSERT(ck1 != 0, "Fletcher-64 checksum must be non-zero");

    // Modifying o_cksum field must not change checksum
    buf[0] = 0xDEADBEEFu;
    buf[1] = 0xCAFEBABEu;
    const std::uint64_t ck2 = ApfsParser::compute_fletcher64(buf, sizeof(buf));
    APFS_TEST_ASSERT(ck1 == ck2, "Modifying o_cksum header must not alter computed Fletcher-64 checksum");

    // Modifying payload word must alter checksum
    buf[2] ^= 0xFFFFFFFFu;
    const std::uint64_t ck3 = ApfsParser::compute_fletcher64(buf, sizeof(buf));
    APFS_TEST_ASSERT(ck1 != ck3, "Mutating payload must alter Fletcher-64 checksum");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Volume Superblock Discovery & Multi-Volume Topology
// -----------------------------------------------------------------------------
bool test_volume_discovery_and_topology() {
    std::cout << "[RUN] Test 3: Multi-Volume Discovery & Topology ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    APFS_TEST_ASSERT(parser.load_container(img.data(), img.size(), err), "Container load must succeed");
    APFS_TEST_ASSERT(parser.volume_count() == 3, "Container must contain exactly 3 volumes");

    auto sys_vol = parser.find_volume_by_name("iPhone OS");
    APFS_TEST_ASSERT(sys_vol.has_value(), "System volume 'iPhone OS' must be discovered");
    APFS_TEST_ASSERT(sys_vol->index == 0, "System volume index must be 0");

    auto data_vol = parser.find_volume_by_name("Data");
    APFS_TEST_ASSERT(data_vol.has_value(), "Data volume 'Data' must be discovered");
    APFS_TEST_ASSERT(data_vol->index == 1, "Data volume index must be 1");

    auto preboot_vol = parser.find_volume_by_name("Preboot");
    APFS_TEST_ASSERT(preboot_vol.has_value(), "Preboot volume 'Preboot' must be discovered");
    APFS_TEST_ASSERT(preboot_vol->index == 2, "Preboot volume index must be 2");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: APFS Volume Role Identification (System, Data, Preboot)
// -----------------------------------------------------------------------------
bool test_volume_roles_and_identities() {
    std::cout << "[RUN] Test 4: Volume Role Identification (System, Data, Preboot) ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    auto sys_opt = parser.find_volume_by_role(ApfsVolumeRole::System);
    APFS_TEST_ASSERT(sys_opt.has_value(), "Volume with System role must exist");
    APFS_TEST_ASSERT(sys_opt->role == ApfsVolumeRole::System, "Role must match System (0x0001)");
    APFS_TEST_ASSERT(sys_opt->name == "iPhone OS", "System volume name must be iPhone OS");
    APFS_TEST_ASSERT(!sys_opt->uuid_hex.empty(), "Volume UUID must be formatted");

    auto data_opt = parser.find_volume_by_role(ApfsVolumeRole::User);
    APFS_TEST_ASSERT(data_opt.has_value(), "Volume with User/Data role must exist");

    auto preboot_opt = parser.find_volume_by_role(ApfsVolumeRole::Preboot);
    APFS_TEST_ASSERT(preboot_opt.has_value(), "Volume with Preboot role must exist");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Volume Selection & Root Directory Inode Resolution
// -----------------------------------------------------------------------------
bool test_volume_selection_and_root_inode() {
    std::cout << "[RUN] Test 5: Volume Selection & Root Directory Inode ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    APFS_TEST_ASSERT(parser.select_volume(0, err), "Selecting volume 0 must succeed");

    auto root_ino = parser.lookup_inode(1);
    APFS_TEST_ASSERT(root_ino.has_value(), "Root inode 1 must exist");
    APFS_TEST_ASSERT(root_ino->is_directory(), "Root inode must be a directory");
    APFS_TEST_ASSERT(!root_ino->is_regular_file(), "Root inode must not be a regular file");
    APFS_TEST_ASSERT((root_ino->mode & kApfsModeTypeMask) == kApfsModeDir, "Mode must have directory flag");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Root Directory Child Enumeration & Inode Hierarchy
// -----------------------------------------------------------------------------
bool test_root_directory_enumeration() {
    std::cout << "[RUN] Test 6: Root Directory Child Enumeration ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    auto entries = parser.read_directory(1);
    APFS_TEST_ASSERT(!entries.empty(), "Root directory must not be empty");

    bool has_sbin = false;
    bool has_system = false;
    for (const auto& e : entries) {
        if (e.name == "sbin") {
            has_sbin = true;
            APFS_TEST_ASSERT(e.type == 4, "sbin entry type must be directory (DT_DIR = 4)");
        }
        if (e.name == "System") {
            has_system = true;
            APFS_TEST_ASSERT(e.type == 4, "System entry type must be directory (DT_DIR = 4)");
        }
    }

    APFS_TEST_ASSERT(has_sbin, "Root directory must contain 'sbin'");
    APFS_TEST_ASSERT(has_system, "Root directory must contain 'System'");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Deep Path Traversal
// -----------------------------------------------------------------------------
bool test_deep_path_traversal() {
    std::cout << "[RUN] Test 7: Deep Path Traversal ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    // Root directory traversal
    auto root_path = parser.lookup_path("/");
    APFS_TEST_ASSERT(root_path.has_value() && root_path->inode_id == 1, "Path '/' must resolve to inode 1");

    // Intermediate directory traversal
    auto sbin_path = parser.lookup_path("/sbin");
    APFS_TEST_ASSERT(sbin_path.has_value() && sbin_path->is_directory(), "Path '/sbin' must resolve to directory");

    // Leaf file traversal
    auto launchd_path = parser.lookup_path("/sbin/launchd");
    APFS_TEST_ASSERT(launchd_path.has_value() && launchd_path->is_regular_file(), "Path '/sbin/launchd' must resolve to file");

    // Nested path traversal
    auto plist_path = parser.lookup_path("/System/Library/CoreServices/SystemVersion.plist");
    APFS_TEST_ASSERT(plist_path.has_value() && plist_path->is_regular_file(),
                     "Path '/System/Library/CoreServices/SystemVersion.plist' must resolve to regular file");

    // Non-existent path
    auto invalid_path = parser.lookup_path("/var/log/missing.log");
    APFS_TEST_ASSERT(!invalid_path.has_value(), "Non-existent path must return nullopt");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Mach-O 64-bit Launchd Binary Header Inspection
// -----------------------------------------------------------------------------
bool test_launchd_binary_macho_header() {
    std::cout << "[RUN] Test 8: Mach-O 64-bit Launchd Binary Header Inspection ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    std::vector<std::uint8_t> launchd_bytes;
    APFS_TEST_ASSERT(parser.read_file_by_path("/sbin/launchd", launchd_bytes, err), "Reading /sbin/launchd must succeed");
    APFS_TEST_ASSERT(launchd_bytes.size() >= 16, "Launchd binary must contain at least 16-byte Mach-O header");

    const std::uint32_t magic = *reinterpret_cast<const std::uint32_t*>(launchd_bytes.data());
    const std::uint32_t cputype = *reinterpret_cast<const std::uint32_t*>(launchd_bytes.data() + 4);
    const std::uint32_t cpusubtype = *reinterpret_cast<const std::uint32_t*>(launchd_bytes.data() + 8);
    const std::uint32_t filetype = *reinterpret_cast<const std::uint32_t*>(launchd_bytes.data() + 12);

    APFS_TEST_ASSERT(magic == 0xFEEDFACFu, "Magic must be MH_MAGIC_64 (0xFEEDFACF)");
    APFS_TEST_ASSERT(cputype == 0x0100000Cu, "CPU type must be CPU_TYPE_ARM64 (0x0100000C)");
    APFS_TEST_ASSERT(cpusubtype == 0x80000002u, "CPU subtype must be CPU_SUBTYPE_ARM64E (0x80000002)");
    APFS_TEST_ASSERT(filetype == 0x00000002u, "File type must be MH_EXECUTE (0x00000002)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: SystemVersion.plist Payload Parsing & iOS Conformance
// -----------------------------------------------------------------------------
bool test_system_version_plist() {
    std::cout << "[RUN] Test 9: SystemVersion.plist Payload Parsing ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container("17.4", "21E219");
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    std::vector<std::uint8_t> plist_bytes;
    APFS_TEST_ASSERT(parser.read_file_by_path("/System/Library/CoreServices/SystemVersion.plist", plist_bytes, err),
                     "Reading SystemVersion.plist must succeed");

    std::string plist_str(plist_bytes.begin(), plist_bytes.end());
    APFS_TEST_ASSERT(plist_str.find("<key>ProductVersion</key>") != std::string::npos, "Must contain ProductVersion key");
    APFS_TEST_ASSERT(plist_str.find("<string>17.4</string>") != std::string::npos, "ProductVersion must be 17.4");
    APFS_TEST_ASSERT(plist_str.find("<key>ProductBuildVersion</key>") != std::string::npos, "Must contain ProductBuildVersion");
    APFS_TEST_ASSERT(plist_str.find("<string>21E219</string>") != std::string::npos, "BuildTrain must be 21E219");
    APFS_TEST_ASSERT(plist_str.find("<string>iPhone OS</string>") != std::string::npos, "ProductName must be iPhone OS");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Multi-Volume Switching & Active State Isolation
// -----------------------------------------------------------------------------
bool test_multi_volume_switching() {
    std::cout << "[RUN] Test 10: Multi-Volume Switching & State Isolation ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    // Switch to volume 1 ("Data")
    APFS_TEST_ASSERT(parser.select_volume(1, err), "Selecting Data volume must succeed");

    // Switch back to volume 0 ("iPhone OS")
    APFS_TEST_ASSERT(parser.select_volume(0, err), "Switching back to System volume must succeed");
    auto launchd = parser.lookup_path("/sbin/launchd");
    APFS_TEST_ASSERT(launchd.has_value(), "System volume path lookup must remain functional after switching");

    // Invalid index rejection
    APFS_TEST_ASSERT(!parser.select_volume(99, err), "Invalid volume index 99 must be rejected");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 116: Read-Only APFS Minimal Volume Parser Suite          \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_container_superblock_validation()) passed++;
    if (test_fletcher64_checksum()) passed++;
    if (test_volume_discovery_and_topology()) passed++;
    if (test_volume_roles_and_identities()) passed++;
    if (test_volume_selection_and_root_inode()) passed++;
    if (test_root_directory_enumeration()) passed++;
    if (test_deep_path_traversal()) passed++;
    if (test_launchd_binary_macho_header()) passed++;
    if (test_system_version_plist()) passed++;
    if (test_multi_volume_switching()) passed++;

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << passed << " / " << total << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
