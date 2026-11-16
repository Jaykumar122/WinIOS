#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "loader/kernel_image_parser.h"
#include "memory/kernel_memory_layout.h"

using namespace winios::memory;
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

void test_canonical_layout() {
    std::cout << "[Test 1] Canonical Kernel Memory Layout...\n";
    KernelMemoryLayout layout(0x40000000ULL, 0x10000000ULL); // 256MB DRAM

    std::string err;
    TEST_CHECK(layout.validate_layout(&err), "Canonical layout must be valid: " + err);
    TEST_CHECK(layout.check_wx_policy(&err), "Canonical layout must obey W^X: " + err);

    auto* text = layout.find_region(kKernelTextBase);
    TEST_CHECK(text != nullptr, "Must locate KernelText");
    TEST_CHECK(text->is_executable && !text->is_writable, "KernelText must be R-X");

    auto* data = layout.find_region(kKernelDataBase);
    TEST_CHECK(data != nullptr, "Must locate KernelData");
    TEST_CHECK(!data->is_executable && data->is_writable, "KernelData must be RW-");

    auto* heap = layout.find_region(kKernelHeapBase);
    TEST_CHECK(heap != nullptr, "Must locate KernelHeap");
    TEST_CHECK(!heap->is_executable && heap->is_writable, "KernelHeap must be RW-");

    // Guard pages check
    TEST_CHECK(layout.is_guard_page(kKernelHeapBase - kGuardPageSize), "Pre-heap guard must be active");
    TEST_CHECK(layout.is_guard_page(kKernelStackBase - kGuardPageSize), "Pre-stack guard must be active");
    std::cout << "  -> Canonical layout passed.\n";
}

void test_build_from_parsed_image() {
    std::cout << "[Test 2] Build Layout from Parsed Kernel Image...\n";
    KernelParsedImage image;
    image.format = KernelImageFormat::MachO64;

    image.segments.push_back(KernelSegmentInfo{
        .name = "__TEXT",
        .vmaddr = 0xFFFFFE0007004000ULL,
        .vmsize = 0x8000, // 32 KB
        .fileoff = 0,
        .filesize = 0x8000,
        .maxprot = 5,
        .initprot = 5, // R-X
        .nsects = 2,
        .is_executable = true,
        .is_writable = false
    });

    image.segments.push_back(KernelSegmentInfo{
        .name = "__DATA_CONST",
        .vmaddr = 0xFFFFFE000700C000ULL,
        .vmsize = 0x4000, // 16 KB
        .fileoff = 0x8000,
        .filesize = 0x4000,
        .maxprot = 1,
        .initprot = 1, // R--
        .nsects = 1,
        .is_executable = false,
        .is_writable = false
    });

    image.segments.push_back(KernelSegmentInfo{
        .name = "__DATA",
        .vmaddr = 0xFFFFFE0007010000ULL,
        .vmsize = 0x4000, // 16 KB
        .fileoff = 0xC000,
        .filesize = 0x4000,
        .maxprot = 3,
        .initprot = 3, // RW-
        .nsects = 1,
        .is_executable = false,
        .is_writable = true
    });

    KernelMemoryLayout layout;
    std::string err;
    bool ok = layout.build_from_parsed_image(
        image,
        0x800000000ULL, // RAM Base (iPhone standard)
        0x40000000ULL,  // 1 GB RAM
        0x1000,         // Boot args (4KB)
        0x4000,         // Device tree (16KB)
        &err);

    TEST_CHECK(ok, "Build from image must succeed: " + err);
    TEST_CHECK(layout.check_wx_policy(&err), "Image layout must obey W^X: " + err);

    auto* text = layout.find_region_by_kind(MemoryRegionKind::KernelText);
    TEST_CHECK(text != nullptr, "Must find KernelText");
    TEST_CHECK(text->is_executable && !text->is_writable, "Text must be R-X");

    auto* rodata = layout.find_region_by_kind(MemoryRegionKind::ReadOnlyData);
    TEST_CHECK(rodata != nullptr, "Must find ReadOnlyData");
    TEST_CHECK(rodata->is_readable && !rodata->is_writable && !rodata->is_executable, "ROData must be R--");

    auto* boot = layout.find_region_by_kind(MemoryRegionKind::BootInfo);
    TEST_CHECK(boot != nullptr, "Must find BootInfo");

    auto* dt = layout.find_region_by_kind(MemoryRegionKind::DeviceTree);
    TEST_CHECK(dt != nullptr, "Must find DeviceTree");

    auto* pt = layout.find_region_by_kind(MemoryRegionKind::PageTables);
    TEST_CHECK(pt != nullptr, "Must find PageTables");

    auto* stack = layout.find_region_by_kind(MemoryRegionKind::KernelStack);
    TEST_CHECK(stack != nullptr, "Must find KernelStack");
    std::cout << "  -> Build from parsed image passed.\n";
}

void test_wx_violation_detection() {
    std::cout << "[Test 3] W^X Policy Violation Detection...\n";
    KernelMemoryLayout layout;
    KernelMemoryRegion bad_reg{
        .name = "InsecureRegion",
        .kind = MemoryRegionKind::KernelText,
        .virtual_address = 0xFFFF800010000000ULL,
        .size = 0x4000,
        .physical_address = 0x50000000ULL,
        .is_readable = true,
        .is_writable = true,     // Simultaneously Writable
        .is_executable = true,   // and Executable!
        .is_guard = false
    };

    std::string err;
    bool added = layout.add_region(bad_reg, &err);
    TEST_CHECK(!added, "Must reject adding simultaneously W and X region");
    TEST_CHECK(err.find("W^X") != std::string::npos, "Error must mention W^X violation");
    std::cout << "  -> W^X violation detection passed.\n";
}

void test_overlap_and_alignment_rejection() {
    std::cout << "[Test 4] Overlap & Alignment Rejection...\n";
    KernelMemoryLayout layout;
    layout.clear_regions();

    KernelMemoryRegion reg1{
        .name = "Region1",
        .kind = MemoryRegionKind::KernelText,
        .virtual_address = 0xFFFF800000000000ULL,
        .size = 0x8000,
        .physical_address = 0x40000000ULL,
        .is_readable = true,
        .is_writable = false,
        .is_executable = true,
        .is_guard = false
    };
    std::string err;
    TEST_CHECK(layout.add_region(reg1, &err), "Add Region1 must succeed");

    // Overlapping region (starts at 0xFFFF800000004000, inside Region1)
    KernelMemoryRegion reg2 = reg1;
    reg2.name = "Region2";
    reg2.virtual_address = 0xFFFF800000004000ULL;
    TEST_CHECK(!layout.add_region(reg2, &err), "Must reject overlapping Region2");

    // Misaligned region (starts at 0xFFFF800000010001)
    KernelMemoryRegion reg3 = reg1;
    reg3.name = "Region3";
    reg3.virtual_address = 0xFFFF800000010001ULL;
    TEST_CHECK(!layout.add_region(reg3, &err), "Must reject misaligned Region3");
    std::cout << "  -> Overlap & alignment rejection passed.\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Running Stage 205.5 Kernel Memory Layout Tests ===\n";
    test_canonical_layout();
    test_build_from_parsed_image();
    test_wx_violation_detection();
    test_overlap_and_alignment_rejection();

    if (g_failures == 0) {
        std::cout << "ALL STAGE 205.5 KERNEL MEMORY LAYOUT TESTS PASSED!\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED!\n";
        return 1;
    }
}
