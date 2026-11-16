#include <cstdint>
#include <iostream>
#include <string>

#include "memory/boot_memory_descriptor.h"

using namespace winios::memory;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_valid_boot_memory_map() {
    std::cout << "[Test 1] Valid Boot Memory Map...\n";
    std::uint64_t ram_base = 0x800000000ULL; // 32 GB base
    std::uint64_t ram_size = 0x80000000ULL;  // 2 GB RAM

    BootMemoryMap bmap(ram_base, ram_size);

    std::string err;
    // 1. Kernel Image (64 MB)
    TEST_CHECK(bmap.add_range({
        .type = BootMemoryType::KernelImage,
        .phys_start = ram_base,
        .size = 0x04000000ULL,
        .virt_start = 0xFFFFFE0007000000ULL,
        .name = "KernelImage"
    }, &err), "Add KernelImage must succeed: " + err);

    // 2. Boot Arguments (16 KB)
    TEST_CHECK(bmap.add_range({
        .type = BootMemoryType::BootArgs,
        .phys_start = ram_base + 0x04000000ULL,
        .size = 0x4000ULL,
        .virt_start = 0xFFFFFE000B000000ULL,
        .name = "BootArgs"
    }, &err), "Add BootArgs must succeed: " + err);

    // 3. Device Tree (64 KB)
    TEST_CHECK(bmap.add_range({
        .type = BootMemoryType::DeviceTree,
        .phys_start = ram_base + 0x04004000ULL,
        .size = 0x10000ULL,
        .virt_start = 0xFFFFFE000B004000ULL,
        .name = "DeviceTree"
    }, &err), "Add DeviceTree must succeed: " + err);

    // 4. Page Tables (64 KB)
    TEST_CHECK(bmap.add_range({
        .type = BootMemoryType::PageTables,
        .phys_start = ram_base + 0x04014000ULL,
        .size = 0x10000ULL,
        .virt_start = 0xFFFFFE000B014000ULL,
        .name = "BootstrapPageTables"
    }, &err), "Add PageTables must succeed: " + err);

    // 5. Initial Stack (64 KB)
    TEST_CHECK(bmap.add_range({
        .type = BootMemoryType::InitialStack,
        .phys_start = ram_base + 0x04024000ULL,
        .size = 0x10000ULL,
        .virt_start = 0xFFFFFE000B024000ULL,
        .name = "InitialKernelStack"
    }, &err), "Add InitialStack must succeed: " + err);

    // 6. Usable RAM remainder
    std::uint64_t used = 0x04034000ULL;
    TEST_CHECK(bmap.add_range({
        .type = BootMemoryType::UsableRAM,
        .phys_start = ram_base + used,
        .size = ram_size - used,
        .virt_start = 0,
        .name = "FreeDRAM"
    }, &err), "Add UsableRAM must succeed: " + err);

    TEST_CHECK(bmap.validate(48, &err), "Validation with 48-bit address width must succeed: " + err);
    TEST_CHECK(bmap.total_reserved_ram() == used, "Reserved RAM must match sum of non-usable regions");
    TEST_CHECK(bmap.total_usable_ram() == (ram_size - used), "Usable RAM must match free space");

    std::string summary = bmap.serialize_summary();
    TEST_CHECK(summary.find("KernelImage") != std::string::npos, "Summary must contain KernelImage");
    TEST_CHECK(summary.find("FreeDRAM") != std::string::npos, "Summary must contain FreeDRAM");

    std::cout << "  -> Valid boot memory map passed.\n";
}

void test_overlap_and_bounds_rejection() {
    std::cout << "[Test 2] Overlap & Bounds Rejection...\n";
    BootMemoryMap bmap(0x40000000ULL, 0x10000000ULL); // 256 MB RAM

    std::string err;
    TEST_CHECK(bmap.add_range({
        .type = BootMemoryType::KernelImage,
        .phys_start = 0x40000000ULL,
        .size = 0x1000000ULL, // 16 MB
        .virt_start = 0xFFFF800000000000ULL,
        .name = "KernelImage"
    }, &err), "Add initial range");

    // Overlapping range
    bool overlap_ok = bmap.add_range({
        .type = BootMemoryType::PageTables,
        .phys_start = 0x40800000ULL, // 8 MB inside KernelImage
        .size = 0x10000ULL,
        .virt_start = 0xFFFF800000800000ULL,
        .name = "OverlappingPT"
    }, &err);
    TEST_CHECK(!overlap_ok, "Must reject overlapping range");
    TEST_CHECK(err.find("overlaps") != std::string::npos, "Error must describe overlap");

    // Out-of-RAM bounds
    BootMemoryMap bmap2(0x40000000ULL, 0x10000000ULL);
    TEST_CHECK(bmap2.add_range({
        .type = BootMemoryType::UsableRAM,
        .phys_start = 0x55000000ULL, // Outside 256MB RAM window
        .size = 0x100000ULL,
        .virt_start = 0,
        .name = "OutOfBoundsRange"
    }), "Add out of bounds range");
    TEST_CHECK(!bmap2.validate(48, &err), "Validation must fail when range exceeds RAM bounds");

    // Address width exceeded
    BootMemoryMap bmap3(0, 0);
    TEST_CHECK(bmap3.add_range({
        .type = BootMemoryType::MMIO,
        .phys_start = 0x100000000ULL, // Needs 33 bits
        .size = 0x1000ULL,
        .virt_start = 0,
        .name = "HighMmio"
    }), "Add 33-bit MMIO");
    TEST_CHECK(!bmap3.validate(32, &err), "Validation must fail when address exceeds 32-bit width");

    std::cout << "  -> Overlap & bounds rejection passed.\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Running Stage 205.7 Boot Memory Descriptor Tests ===\n";
    test_valid_boot_memory_map();
    test_overlap_and_bounds_rejection();

    if (g_failures == 0) {
        std::cout << "ALL STAGE 205.7 BOOT MEMORY DESCRIPTOR TESTS PASSED!\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED!\n";
        return 1;
    }
}
