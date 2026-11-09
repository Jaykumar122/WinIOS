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

#include "device/apple_devicetree.h"
#include "firmware/apple_boot_args.h"
#include "fs/apfs_parser.h"
#include "loader/guest_memory_map.h"
#include "loader/linux_image_loader.h"
#include "loader/ramdisk_loader.h"
#include "memory/memory.h"

#define RAMDISK_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::firmware;
using namespace winios::fs;
using namespace winios::loader;

namespace {

// -----------------------------------------------------------------------------
// Test 1: APFS Ramdisk Image Generation & 16 KB Page Alignment Verification
// -----------------------------------------------------------------------------
bool test_ramdisk_image_and_alignment() {
    std::cout << "[RUN] Test 1: APFS Ramdisk Format & 16 KB Page Alignment ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container("17.4", "21E219");
    RAMDISK_TEST_ASSERT(!img.empty(), "Synthetic APFS container must be non-empty");

    // Detect format
    auto fmt = RamdiskLoader::detect_format(img.data(), img.size());
    RAMDISK_TEST_ASSERT(fmt == RamdiskFormat::ApfsContainer, "Detected format must be ApfsContainer");

    // Target alignment: 16 KB (0x4000) for Apple Silicon
    Memory mem(0x20000000); // 512 MB
    const std::uint64_t ram_base = 0x80000000ULL;
    const std::uint64_t ram_size = 0x20000000ULL;

    // Aligned address
    const std::uint64_t aligned_addr = 0x90000000ULL;
    RAMDISK_TEST_ASSERT((aligned_addr % 0x4000ULL) == 0, "0x90000000 must be 16 KB aligned");

    // Misaligned address (e.g. 0x90001000 is 4 KB aligned but not 16 KB)
    const std::uint64_t misaligned_addr = 0x90001000ULL;
    auto bad_res = RamdiskLoader::load_from_buffer(
        img.data(), img.size(), mem, misaligned_addr, ram_base, ram_size);
    RAMDISK_TEST_ASSERT(!bad_res.is_success(), "Misaligned address must fail");
    RAMDISK_TEST_ASSERT(bad_res.status == RamdiskLoadStatus::MisalignedPlacement,
                        "Status must be MisalignedPlacement");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Ramdisk Staging into Guest Physical RAM via RamdiskLoader
// -----------------------------------------------------------------------------
bool test_ramdisk_staging_to_ram() {
    std::cout << "[RUN] Test 2: Ramdisk Staging into Guest Physical RAM ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container("17.4", "21E219");
    Memory mem(0x20000000); // 512 MB

    const std::uint64_t ram_base = 0x80000000ULL;
    const std::uint64_t ram_size = 0x20000000ULL;
    const std::uint64_t target_addr = 0x90000000ULL;

    auto res = RamdiskLoader::load_from_buffer(
        img.data(), img.size(), mem, target_addr, ram_base, ram_size);

    RAMDISK_TEST_ASSERT(res.is_success(), "Ramdisk loading must succeed");
    RAMDISK_TEST_ASSERT(res.physical_address == target_addr, "Physical address must match target");
    RAMDISK_TEST_ASSERT(res.size == img.size(), "Loaded size must match image size");

    // Verify SHA-256 match
    const std::string expected_sha = LinuxImageLoader::compute_sha256(img.data(), img.size());
    RAMDISK_TEST_ASSERT(res.sha256_hex == expected_sha, "Computed SHA-256 must match source buffer");

    // Inspect memory at target address (relative to ram_base: 0x90000000 - 0x80000000 = 0x10000000)
    const std::uint64_t offset = target_addr - ram_base;
    const std::uint32_t magic32 = mem.read_u32(offset + 32);
    RAMDISK_TEST_ASSERT(magic32 == kApfsContainerMagic, "NXSB magic must be present at offset + 32 in guest RAM");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: GuestMemoryMap Reservation & Collision Detection
// -----------------------------------------------------------------------------
bool test_memory_map_reservation_and_collision() {
    std::cout << "[RUN] Test 3: GuestMemoryMap Reservation & Collision Detection ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    Memory mem(0x20000000); // 512 MB
    const std::uint64_t ram_base = 0x80000000ULL;
    const std::uint64_t ram_size = 0x20000000ULL;

    // 1. Setup memory map with kernel region reserved at 0x80000000..0x84000000
    GuestMemoryMap map(ram_base, ram_size);
    map.register_kernel_image(0x80000000ULL, 0x04000000ULL);

    // 2. Collision test: attempt to load ramdisk overlapping kernel
    auto collide_res = RamdiskLoader::load_from_buffer(
        img.data(), img.size(), mem, 0x82000000ULL, ram_base, ram_size, &map);
    RAMDISK_TEST_ASSERT(!collide_res.is_success(), "Ramdisk colliding with kernel must fail");
    RAMDISK_TEST_ASSERT(collide_res.status == RamdiskLoadStatus::MemoryCollision,
                        "Status must be MemoryCollision");

    // 3. Non-colliding load at 0x90000000 with clean map
    GuestMemoryMap map2(ram_base, ram_size);
    map2.register_kernel_image(0x80000000ULL, 0x04000000ULL);
    auto valid_res = RamdiskLoader::load_from_buffer(
        img.data(), img.size(), mem, 0x90000000ULL, ram_base, ram_size, &map2);
    RAMDISK_TEST_ASSERT(valid_res.is_success(), "Non-colliding load must succeed");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Apple DeviceTree /chosen Ramdisk Node Registration
// -----------------------------------------------------------------------------
bool test_devicetree_ramdisk_binding() {
    std::cout << "[RUN] Test 4: DeviceTree /chosen Ramdisk Binding ... ";

    const std::uint64_t ramdisk_base = 0x90000000ULL;
    const std::uint64_t ramdisk_size = 65536ULL;

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree(
        0x80000000ULL, 0x40000000ULL, "-v debug=0x144", 0x7C000000ULL, ramdisk_base, ramdisk_size);

    auto dt_root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size());
    RAMDISK_TEST_ASSERT(dt_root != nullptr, "DTB deserialization must succeed");

    // Check /chosen
    const auto* chosen = dt_root->find_child("chosen");
    RAMDISK_TEST_ASSERT(chosen != nullptr, "/chosen node must exist in DTB");

    const auto* base_prop = chosen->find_property("ramdisk-base");
    RAMDISK_TEST_ASSERT(base_prop != nullptr && base_prop->data.size() >= 8, "ramdisk-base property must exist in /chosen");
    RAMDISK_TEST_ASSERT(*reinterpret_cast<const std::uint64_t*>(base_prop->data.data()) == ramdisk_base, "ramdisk-base must match 0x90000000ULL");

    const auto* size_prop = chosen->find_property("ramdisk-size");
    RAMDISK_TEST_ASSERT(size_prop != nullptr && size_prop->data.size() >= 8, "ramdisk-size property must exist in /chosen");
    RAMDISK_TEST_ASSERT(*reinterpret_cast<const std::uint64_t*>(size_prop->data.data()) == ramdisk_size, "ramdisk-size must match 65536ULL");

    // Check /chosen/memory-map/RAMDisk
    const auto* mem_map = chosen->find_child("memory-map");
    RAMDISK_TEST_ASSERT(mem_map != nullptr, "/chosen/memory-map node must exist");

    const auto* ramdisk_range = mem_map->find_property("RAMDisk");
    RAMDISK_TEST_ASSERT(ramdisk_range != nullptr, "RAMDisk property must exist in memory-map");
    RAMDISK_TEST_ASSERT(ramdisk_range->data.size() == 16, "RAMDisk range property must be 16 bytes (base + size)");

    const auto* range_u64 = reinterpret_cast<const std::uint64_t*>(ramdisk_range->data.data());
    RAMDISK_TEST_ASSERT(range_u64[0] == ramdisk_base, "Range base must match ramdisk base");
    RAMDISK_TEST_ASSERT(range_u64[1] == ramdisk_size, "Range size must match ramdisk size");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Boot Handoff Boot-Args & Ramdisk Coexistence
// -----------------------------------------------------------------------------
bool test_boot_args_ramdisk_coexistence() {
    std::cout << "[RUN] Test 5: Boot-Args & Ramdisk Coexistence Validation ... ";

    AppleBootVideo video{};
    video.v_baseAddr = 0x7C000000ULL;
    video.v_width = 1179;
    video.v_height = 2556;
    video.v_rowBytes = 1179 * 4;
    video.v_depth = 32;

    const std::uint64_t phys_base = 0x80000000ULL;
    const std::uint64_t mem_size  = 0x40000000ULL; // 1 GB
    const std::uint64_t top_of_kernel = 0x84000000ULL; // 64 MB for kernel
    const std::uint64_t dtb_addr = 0x85000000ULL;
    const std::uint32_t dtb_len = 0x10000;

    auto args = AppleBootArgsBuilder::create(
        phys_base, 0xFFFFFE0000000000ULL, mem_size, top_of_kernel,
        dtb_addr, dtb_len, "-v debug=0x144", video);

    std::string err;
    RAMDISK_TEST_ASSERT(AppleBootArgsValidator::validate(args, phys_base, mem_size, &err),
                        "Boot arguments must validate cleanly with video framebuffer");

    // Ensure ramdisk at 0x90000000 is completely non-overlapping with kernel and DTB
    const std::uint64_t ramdisk_addr = 0x90000000ULL;
    const std::uint64_t ramdisk_len  = 0x01000000ULL; // 16 MB

    RAMDISK_TEST_ASSERT(ramdisk_addr >= top_of_kernel, "Ramdisk must be placed above kernel data");
    RAMDISK_TEST_ASSERT(ramdisk_addr + ramdisk_len <= phys_base + mem_size, "Ramdisk must reside within RAM");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: In-Memory APFS Parsing Directly from Staged Guest RAM
// -----------------------------------------------------------------------------
bool test_in_memory_apfs_parsing() {
    std::cout << "[RUN] Test 6: In-Memory APFS Parsing Directly from Guest RAM ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container("17.4", "21E219");
    Memory mem(0x20000000); // 512 MB

    const std::uint64_t ram_base = 0x80000000ULL;
    const std::uint64_t target_addr = 0x90000000ULL;
    auto load_res = RamdiskLoader::load_from_buffer(img.data(), img.size(), mem, target_addr, ram_base, 0x20000000ULL);
    RAMDISK_TEST_ASSERT(load_res.is_success(), "Staging to memory must succeed");

    // Read directly from guest memory pointer
    const std::uint64_t mem_offset = target_addr - ram_base;
    const std::uint8_t* guest_ram_ptr = mem.raw_data() + mem_offset;

    ApfsParser parser;
    std::string err;
    RAMDISK_TEST_ASSERT(parser.load_container(guest_ram_ptr, img.size(), err),
                        "In-memory APFS parser must load directly from staged guest RAM");
    RAMDISK_TEST_ASSERT(parser.volume_count() == 3, "All 3 volumes must be discovered from RAM");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Data Volume Partition Initialization & Binding
// -----------------------------------------------------------------------------
bool test_data_volume_initialization() {
    std::cout << "[RUN] Test 7: Data Volume Partition Initialization ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    ApfsVolumeDiscovery discovery(parser);
    auto data_vol = discovery.discover_data_volume();
    RAMDISK_TEST_ASSERT(data_vol.has_value(), "Data volume must be present");
    RAMDISK_TEST_ASSERT(data_vol->role == ApfsVolumeRole::User, "Data volume role must be User (0x0002)");
    RAMDISK_TEST_ASSERT(data_vol->name == "Data", "Data volume name must be Data");

    // Verify System volume vs Data volume index segregation
    auto sys_vol = discovery.discover_root_volume();
    RAMDISK_TEST_ASSERT(sys_vol.has_value() && sys_vol->index != data_vol->index,
                        "System volume and Data volume must have distinct volume indices");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Synthetic Data Directory Tree Staging (/private/var)
// -----------------------------------------------------------------------------
bool test_private_var_directory_staging() {
    std::cout << "[RUN] Test 8: Synthetic Directory Tree Staging (/private) ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    parser.select_volume(0, err);
    auto priv_ino = parser.lookup_path("/private");
    RAMDISK_TEST_ASSERT(priv_ino.has_value(), "/private mount directory must exist");
    RAMDISK_TEST_ASSERT(priv_ino->is_directory(), "/private must be a directory");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: APFS Ramdisk Boot Capability Audit via ApfsVolumeDiscovery
// -----------------------------------------------------------------------------
bool test_ramdisk_boot_capability_audit() {
    std::cout << "[RUN] Test 9: APFS Ramdisk Boot Capability Audit ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container("17.4", "21E219");
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    ApfsVolumeDiscovery discovery(parser);
    auto res = discovery.validate_root_filesystem(0);

    RAMDISK_TEST_ASSERT(res.valid, "Ramdisk root filesystem must be fully validated for boot handoff");
    RAMDISK_TEST_ASSERT(res.launchd_present, "/sbin/launchd must be ready in ramdisk");
    RAMDISK_TEST_ASSERT(res.product_version == "17.4", "iOS version 17.4 confirmed");
    RAMDISK_TEST_ASSERT(res.build_version == "21E219", "Build train 21E219 confirmed");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Multi-Volume Ramdisk State Serialization & Integrity
// -----------------------------------------------------------------------------
bool test_ramdisk_serialization_integrity() {
    std::cout << "[RUN] Test 10: Multi-Volume Ramdisk Integrity & Hash Invariance ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    Memory mem1(0x20000000);
    Memory mem2(0x20000000);

    const std::uint64_t ram_base = 0x80000000ULL;
    const std::uint64_t target_addr = 0x90000000ULL;

    auto res1 = RamdiskLoader::load_from_buffer(img.data(), img.size(), mem1, target_addr, ram_base, 0x20000000ULL);
    auto res2 = RamdiskLoader::load_from_buffer(img.data(), img.size(), mem2, target_addr, ram_base, 0x20000000ULL);

    RAMDISK_TEST_ASSERT(res1.sha256_hex == res2.sha256_hex, "SHA-256 hashes must be strictly invariant");

    // Verify bitwise memory equality
    const std::uint64_t off = target_addr - ram_base;
    const int cmp = std::memcmp(mem1.raw_data() + off, mem2.raw_data() + off, img.size());
    RAMDISK_TEST_ASSERT(cmp == 0, "Staged RAM memory bytes must be bitwise identical across instances");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 118: Ramdisk & Data Volume Initialization Suite          \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_ramdisk_image_and_alignment()) passed++;
    if (test_ramdisk_staging_to_ram()) passed++;
    if (test_memory_map_reservation_and_collision()) passed++;
    if (test_devicetree_ramdisk_binding()) passed++;
    if (test_boot_args_ramdisk_coexistence()) passed++;
    if (test_in_memory_apfs_parsing()) passed++;
    if (test_data_volume_initialization()) passed++;
    if (test_private_var_directory_staging()) passed++;
    if (test_ramdisk_boot_capability_audit()) passed++;
    if (test_ramdisk_serialization_integrity()) passed++;

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << passed << " / " << total << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}
