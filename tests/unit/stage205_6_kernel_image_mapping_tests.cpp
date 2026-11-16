#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "loader/kernel_image_mapper.h"
#include "loader/kernel_image_parser.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::loader;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_valid_mapping_and_zerofill() {
    std::cout << "[Test 1] Valid Mapping & BSS Zero-Fill...\n";
    Memory mem(0x20000); // 128 KB
    // Pre-fill memory with garbage pattern
    for (std::size_t i = 0; i < mem.size(); ++i) {
        mem.write_u8(i, 0xAA);
    }

    std::vector<std::uint8_t> raw_image(0x8000, 0x42); // 32 KB raw image filled with 0x42

    KernelParsedImage image;
    image.preferred_base_vaddr = 0xFFFFFE0007000000ULL;

    // Segment 1: __TEXT at 0x0000, filesize 0x2000, vmsize 0x2000
    image.segments.push_back(KernelSegmentInfo{
        .name = "__TEXT",
        .vmaddr = 0xFFFFFE0007000000ULL,
        .vmsize = 0x2000,
        .fileoff = 0,
        .filesize = 0x2000,
        .is_executable = true,
        .is_writable = false
    });

    // Segment 2: __DATA at 0x4000, filesize 0x1000, vmsize 0x3000 (with 0x2000 BSS zero-fill)
    image.segments.push_back(KernelSegmentInfo{
        .name = "__DATA",
        .vmaddr = 0xFFFFFE0007004000ULL,
        .vmsize = 0x3000,
        .fileoff = 0x2000,
        .filesize = 0x1000,
        .is_executable = false,
        .is_writable = true
    });

    auto res = KernelImageMapper::map_image(mem, raw_image.data(), raw_image.size(), image, 0x800000000ULL, 0x100000ULL);

    TEST_CHECK(res.success, "Mapping must succeed");
    TEST_CHECK(res.segments_mapped == 2, "Must map 2 segments");
    TEST_CHECK(res.bytes_mapped == 0x5000, "Bytes mapped must equal 0x2000 + 0x3000");

    // Verify __TEXT content
    for (std::size_t i = 0; i < 0x2000; ++i) {
        TEST_CHECK(mem.read_u8(i) == 0x42, "Text segment content must match image");
    }

    // Verify __DATA initialized content
    for (std::size_t i = 0; i < 0x1000; ++i) {
        TEST_CHECK(mem.read_u8(0x4000 + i) == 0x42, "Data initialized content must match image");
    }

    // Verify __DATA BSS zero-fill (must NOT be 0xAA)
    for (std::size_t i = 0; i < 0x2000; ++i) {
        TEST_CHECK(mem.read_u8(0x5000 + i) == 0, "BSS zero-fill region must be 0");
    }

    std::cout << "  -> Valid mapping and zero-fill passed.\n";
}

void test_mapping_rollback_on_failure() {
    std::cout << "[Test 2] Transactional Rollback on Segment Failure...\n";
    Memory mem(0x20000); // 128 KB
    // Pre-fill memory with marker 0x55
    for (std::size_t i = 0; i < mem.size(); ++i) {
        mem.write_u8(i, 0x55);
    }

    std::vector<std::uint8_t> raw_image(0x8000, 0x77);

    KernelParsedImage image;
    image.preferred_base_vaddr = 0x100000000ULL;

    // Segment 1: valid
    image.segments.push_back(KernelSegmentInfo{
        .name = "__TEXT",
        .vmaddr = 0x100000000ULL,
        .vmsize = 0x2000,
        .fileoff = 0,
        .filesize = 0x2000
    });

    // Segment 2: invalid (exceeds raw_image size 0x8000)
    image.segments.push_back(KernelSegmentInfo{
        .name = "__DATA",
        .vmaddr = 0x100004000ULL,
        .vmsize = 0x2000,
        .fileoff = 0x9000, // Invalid!
        .filesize = 0x2000
    });

    auto res = KernelImageMapper::map_image(mem, raw_image.data(), raw_image.size(), image, 0, 0x100000);

    TEST_CHECK(!res.success, "Mapping must fail on corrupted second segment");
    TEST_CHECK(res.error_message.find("exceeds raw image size") != std::string::npos, "Error must describe file offset fault");

    // Verify rollback: Segment 1 (which was mapped first) must have been reverted to 0!
    for (std::size_t i = 0; i < 0x2000; ++i) {
        TEST_CHECK(mem.read_u8(i) == 0, "Previously written bytes must be rolled back (zeroed)");
    }

    std::cout << "  -> Rollback on failure passed.\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Running Stage 205.6 Kernel Image Mapping Tests ===\n";
    test_valid_mapping_and_zerofill();
    test_mapping_rollback_on_failure();

    if (g_failures == 0) {
        std::cout << "ALL STAGE 205.6 KERNEL IMAGE MAPPING TESTS PASSED!\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED!\n";
        return 1;
    }
}
