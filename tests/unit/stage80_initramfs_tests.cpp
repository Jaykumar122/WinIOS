#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "device/fdt_builder.h"
#include "loader/guest_memory_map.h"
#include "loader/initramfs_loader.h"
#include "memory/memory.h"

#define STAGE80_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::loader;

namespace {

std::vector<std::uint8_t> create_synthetic_cpio(const char* magic_str = "070701", std::size_t size = 4096) {
    std::vector<std::uint8_t> buf(size, 0);
    std::memcpy(buf.data(), magic_str, 6);
    // Fill remainder with dummy content
    for (std::size_t i = 6; i < size; ++i) {
        buf[i] = static_cast<std::uint8_t>((i * 7) & 0xFFu);
    }
    return buf;
}

// -----------------------------------------------------------------------------
// Suite 1: CPIO newc Format Detection and Loading
// -----------------------------------------------------------------------------
bool test_cpio_newc_format_detection_and_loading() {
    std::cout << "[1] test_cpio_newc_format_detection_and_loading...\n";

    auto cpio = create_synthetic_cpio("070701", 8192);
    STAGE80_ASSERT(InitramfsLoader::detect_format(cpio.data(), cpio.size()) == InitramfsFormat::CpioNewc,
                   "Must detect CpioNewc");

    Memory mem(0x08000000ULL); // 128 MB RAM
    auto res = InitramfsLoader::load_from_buffer(
        cpio.data(), cpio.size(), mem, 0x42000000ULL, 0x40000000ULL, 0x08000000ULL);

    STAGE80_ASSERT(res.is_success(), "Loading must succeed");
    STAGE80_ASSERT(res.format == InitramfsFormat::CpioNewc, "Result format must be CpioNewc");
    STAGE80_ASSERT(res.load_address == 0x42000000ULL, "Load address match");
    STAGE80_ASSERT(res.end_address == 0x42000000ULL + 8192, "End address match");
    STAGE80_ASSERT(res.size == 8192, "Size match");

    // Verify RAM content
    std::uint8_t b0 = mem.read_u8(0x42000000ULL - 0x40000000ULL);
    STAGE80_ASSERT(b0 == '0', "First byte in RAM mismatch");

    std::cout << "[PASS] CPIO newc Format Detection and Loading\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: CPIO crc Format Detection
// -----------------------------------------------------------------------------
bool test_cpio_crc_format_detection() {
    std::cout << "[2] test_cpio_crc_format_detection...\n";

    auto cpio = create_synthetic_cpio("070702", 4096);
    STAGE80_ASSERT(InitramfsLoader::detect_format(cpio.data(), cpio.size()) == InitramfsFormat::CpioCrc,
                   "Must detect CpioCrc");

    std::cout << "[PASS] CPIO crc Format Detection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Gzip Compressed Format Detection
// -----------------------------------------------------------------------------
bool test_gzip_compressed_format_detection() {
    std::cout << "[3] test_gzip_compressed_format_detection...\n";

    std::vector<std::uint8_t> gz(1024, 0);
    gz[0] = 0x1F;
    gz[1] = 0x8B; // Gzip magic

    STAGE80_ASSERT(InitramfsLoader::detect_format(gz.data(), gz.size()) == InitramfsFormat::Gzip,
                   "Must detect Gzip");

    std::cout << "[PASS] Gzip Compressed Format Detection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Raw Binary Initramfs Loading
// -----------------------------------------------------------------------------
bool test_raw_binary_initramfs_loading() {
    std::cout << "[4] test_raw_binary_initramfs_loading...\n";

    std::vector<std::uint8_t> raw(4096, 0xEE);
    STAGE80_ASSERT(InitramfsLoader::detect_format(raw.data(), raw.size()) == InitramfsFormat::RawBinary,
                   "Must detect RawBinary");

    Memory mem(0x08000000ULL);
    auto res = InitramfsLoader::load_from_buffer(
        raw.data(), raw.size(), mem, 0x43000000ULL, 0x40000000ULL, 0x08000000ULL);

    STAGE80_ASSERT(res.is_success(), "Raw binary load must succeed");
    STAGE80_ASSERT(res.format == InitramfsFormat::RawBinary, "Format RawBinary");

    std::cout << "[PASS] Raw Binary Initramfs Loading\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: DTB Chosen Node Handoff Integration
// -----------------------------------------------------------------------------
bool test_dtb_chosen_node_handoff_integration() {
    std::cout << "[5] test_dtb_chosen_node_handoff_integration...\n";

    VirtualPlatformConfig config;
    config.ram_base = 0x40000000ULL;
    config.ram_size = 0x08000000ULL;

    std::uint64_t initrd_start = 0x42000000ULL;
    std::uint64_t initrd_end   = 0x42800000ULL;

    auto dtb = FdtBuilder::build_platform_dtb(
        config, "console=ttyAMA0 root=/dev/ram0 rw", initrd_start, initrd_end);

    FdtReader reader(dtb);
    STAGE80_ASSERT(reader.is_valid(), "DTB must be valid");
    STAGE80_ASSERT(reader.get_u64_property("/chosen", "linux,initrd-start") == initrd_start,
                   "initrd-start DTB mismatch");
    STAGE80_ASSERT(reader.get_u64_property("/chosen", "linux,initrd-end") == initrd_end,
                   "initrd-end DTB mismatch");

    std::cout << "[PASS] DTB Chosen Node Handoff Integration\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Initramfs Page Alignment Enforcement
// -----------------------------------------------------------------------------
bool test_initramfs_page_alignment_enforcement() {
    std::cout << "[6] test_initramfs_page_alignment_enforcement...\n";

    auto cpio = create_synthetic_cpio();
    Memory mem(0x08000000ULL);

    // Target address 0x42000010 is not 4KB aligned
    auto res = InitramfsLoader::load_from_buffer(
        cpio.data(), cpio.size(), mem, 0x42000010ULL, 0x40000000ULL, 0x08000000ULL);

    STAGE80_ASSERT(!res.is_success(), "Misaligned target address must fail");
    STAGE80_ASSERT(res.status == InitramfsLoadStatus::MisalignedPlacement, "Status MisalignedPlacement");

    std::cout << "[PASS] Initramfs Page Alignment Enforcement\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Collision Detection Against Kernel and DTB
// -----------------------------------------------------------------------------
bool test_collision_detection_against_kernel_and_dtb() {
    std::cout << "[7] test_collision_detection_against_kernel_and_dtb...\n";

    GuestMemoryMap map(0x40000000ULL, 0x08000000ULL);
    // Kernel at 0x40080000..0x42080000 (32 MB)
    map.register_kernel_image(0x40080000ULL, 0x02000000ULL);
    // DTB at 0x47E00000..0x47F00000
    map.register_dtb(0x47E00000ULL, 0x00100000ULL);

    auto cpio = create_synthetic_cpio("070701", 0x1000000); // 16 MB
    Memory mem(0x08000000ULL);

    // Attempt loading initramfs at 0x41000000 (collides with Kernel!)
    auto res = InitramfsLoader::load_from_buffer(
        cpio.data(), cpio.size(), mem, 0x41000000ULL, 0x40000000ULL, 0x08000000ULL, &map);

    STAGE80_ASSERT(!res.is_success(), "Collision must be detected");
    STAGE80_ASSERT(res.status == InitramfsLoadStatus::MemoryCollision, "Status MemoryCollision");

    std::cout << "[PASS] Collision Detection Against Kernel and DTB\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Load from File with SHA-256
// -----------------------------------------------------------------------------
bool test_load_from_file_with_sha256() {
    std::cout << "[8] test_load_from_file_with_sha256...\n";

    const std::string file_path = "stage80_test_initramfs.cpio";
    auto cpio = create_synthetic_cpio("070701", 16384);

    {
        std::ofstream out(file_path, std::ios::binary);
        STAGE80_ASSERT(out.is_open(), "Cannot create test initramfs file");
        out.write(reinterpret_cast<const char*>(cpio.data()), cpio.size());
    }

    Memory mem(0x08000000ULL);
    auto res = InitramfsLoader::load_from_file(
        file_path, mem, 0x42000000ULL, 0x40000000ULL, 0x08000000ULL);

    STAGE80_ASSERT(res.is_success(), "File load must succeed");
    STAGE80_ASSERT(res.size == 16384, "Size match");
    STAGE80_ASSERT(res.sha256_hex.size() == 64, "SHA-256 string must be 64 hex chars");

    std::cout << "[PASS] Load from File with SHA-256\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: File Not Found and Empty Buffer Rejection
// -----------------------------------------------------------------------------
bool test_file_not_found_and_empty_buffer_rejection() {
    std::cout << "[9] test_file_not_found_and_empty_buffer_rejection...\n";

    Memory mem(0x08000000ULL);

    // 1. Non-existent file
    auto res1 = InitramfsLoader::load_from_file(
        "missing_initramfs_archive_404.cpio", mem, 0x42000000ULL, 0x40000000ULL, 0x08000000ULL);
    STAGE80_ASSERT(!res1.is_success(), "Missing file must fail");
    STAGE80_ASSERT(res1.status == InitramfsLoadStatus::FileNotFound, "Status FileNotFound");

    // 2. Empty buffer
    auto res2 = InitramfsLoader::load_from_buffer(
        nullptr, 0, mem, 0x42000000ULL, 0x40000000ULL, 0x08000000ULL);
    STAGE80_ASSERT(!res2.is_success(), "Empty buffer must fail");
    STAGE80_ASSERT(res2.status == InitramfsLoadStatus::EmptyBuffer, "Status EmptyBuffer");

    std::cout << "[PASS] File Not Found and Empty Buffer Rejection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Memory Write Integrity and Bounds Checking
// -----------------------------------------------------------------------------
bool test_memory_write_integrity_and_bounds_checking() {
    std::cout << "[10] test_memory_write_integrity_and_bounds_checking...\n";

    auto cpio = create_synthetic_cpio("070701", 4096);
    Memory mem(0x08000000ULL);

    // Target extending beyond RAM end (0x48000000)
    auto res = InitramfsLoader::load_from_buffer(
        cpio.data(), cpio.size(), mem, 0x48000000ULL, 0x40000000ULL, 0x08000000ULL);

    STAGE80_ASSERT(!res.is_success(), "Out-of-bounds initramfs must fail");
    STAGE80_ASSERT(res.status == InitramfsLoadStatus::RamCapacityExceeded, "Status RamCapacityExceeded");

    std::cout << "[PASS] Memory Write Integrity and Bounds Checking\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=== WinIOS Stage 80: Initramfs Loading & Root Filesystem Tests ===\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        if (test_fn()) {
            passed++;
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n";
        }
    };

    run_test(test_cpio_newc_format_detection_and_loading, "test_cpio_newc_format_detection_and_loading");
    run_test(test_cpio_crc_format_detection, "test_cpio_crc_format_detection");
    run_test(test_gzip_compressed_format_detection, "test_gzip_compressed_format_detection");
    run_test(test_raw_binary_initramfs_loading, "test_raw_binary_initramfs_loading");
    run_test(test_dtb_chosen_node_handoff_integration, "test_dtb_chosen_node_handoff_integration");
    run_test(test_initramfs_page_alignment_enforcement, "test_initramfs_page_alignment_enforcement");
    run_test(test_collision_detection_against_kernel_and_dtb, "test_collision_detection_against_kernel_and_dtb");
    run_test(test_load_from_file_with_sha256, "test_load_from_file_with_sha256");
    run_test(test_file_not_found_and_empty_buffer_rejection, "test_file_not_found_and_empty_buffer_rejection");
    run_test(test_memory_write_integrity_and_bounds_checking, "test_memory_write_integrity_and_bounds_checking");

    std::cout << "\nStage 80 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
