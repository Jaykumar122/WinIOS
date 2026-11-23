#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "memory/canonical_physical_memory_map.h"
#include "memory/xnu_page_allocator.h"
#include "memory/xnu_zone_allocator.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::memory;

// ----------------------------------------------------------------------------
// Test 1: Stage 261.1 & 261.2: Canonical Memory Map Reconstruction & Representation
// ----------------------------------------------------------------------------
static void test_stage261_1_2_canonical_representation() {
    std::cout << "[Stage 261.1-2] Testing Canonical Physical Memory Map Reconstruction...\n";

    auto map = CanonicalPhysicalMemoryMap::create_canonical_iphone15_layout();

    TEST_ASSERT(map.ram_base() == 0x00000000ULL, "RAM base is 0x0");
    TEST_ASSERT(map.ram_size() == 0x00800000ULL, "RAM size is 8 MiB (512 frames)");
    TEST_ASSERT(map.ram_end()  == 0x00800000ULL, "RAM end is 0x800000");

    // Verify key intervals exist and have authoritative properties
    const auto* ktext1 = map.find_interval_by_name("KernelTextPrimary");
    TEST_ASSERT(ktext1 != nullptr, "KernelTextPrimary exists");
    TEST_ASSERT(ktext1->start_address == 0x00000000ULL, "KernelTextPrimary base is 0x0");
    TEST_ASSERT(ktext1->size == 0x00004000ULL, "KernelTextPrimary size is 16 KiB (Frame 0)");
    TEST_ASSERT(ktext1->permissions == 0x5u, "KernelTextPrimary is RX (Read+Execute)");
    TEST_ASSERT(ktext1->provenance == "Category B", "KernelTextPrimary provenance is Category B");

    const auto* kdata = map.find_interval_by_name("KernelData");
    TEST_ASSERT(kdata != nullptr, "KernelData exists");
    TEST_ASSERT(kdata->start_address == 0x00010000ULL, "KernelData base is 0x10000");
    TEST_ASSERT(kdata->size == 0x00020000ULL, "KernelData size is 128 KiB");
    TEST_ASSERT(kdata->permissions == 0x3u, "KernelData is RW-NX");

    const auto* dt = map.find_interval_by_name("DeviceTree");
    TEST_ASSERT(dt != nullptr && dt->start_address == 0x00030000ULL, "DeviceTree at 0x30000");

    const auto* ba = map.find_interval_by_name("BootArgs");
    TEST_ASSERT(ba != nullptr && ba->start_address == 0x00034000ULL, "BootArgs at 0x34000");

    const auto* pt = map.find_interval_by_name("Stage1PageTables");
    TEST_ASSERT(pt != nullptr && pt->start_address == 0x00048000ULL, "PageTables at 0x48000 (Frames 18-19)");

    // Secondary kernel text at 0x80000
    const auto* ktext2 = map.find_interval_by_name("KernelTextSecondary");
    TEST_ASSERT(ktext2 != nullptr, "KernelTextSecondary exists");
    TEST_ASSERT(ktext2->start_address == 0x00080000ULL, "KernelTextSecondary base is 0x80000 (Frame 32)");
    TEST_ASSERT(ktext2->size == 0x00004000ULL, "KernelTextSecondary size is 16 KiB");
    TEST_ASSERT(ktext2->permissions == 0x5u, "KernelTextSecondary is RX");
    TEST_ASSERT(ktext2->provenance == "Category D", "KernelTextSecondary provenance is Category D");

    std::cout << "[Stage 261.1-2] Canonical physical memory map verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 261.3: Independent Overlap Auditor
// ----------------------------------------------------------------------------
static void test_stage261_3_independent_overlap_auditor() {
    std::cout << "[Stage 261.3] Testing Independent Physical Memory Overlap Auditor...\n";

    auto map = CanonicalPhysicalMemoryMap::create_canonical_iphone15_layout();
    PhysicalMemoryOverlapAuditor auditor(map);

    std::vector<ConflictRecord> conflicts;
    bool no_overlaps = auditor.audit_overlaps(conflicts);
    if (!no_overlaps) {
        for (const auto& c : conflicts) {
            std::cerr << "Conflict: " << c.description << "\n";
        }
    }
    TEST_ASSERT(no_overlaps, "Canonical layout has ZERO overlapping intervals");
    TEST_ASSERT(conflicts.empty(), "Zero conflict records generated");

    std::vector<std::string> wx_violations;
    TEST_ASSERT(auditor.audit_wx_compliance(wx_violations), "Strict W^X compliance verified");

    std::vector<std::string> align_errors;
    TEST_ASSERT(auditor.audit_granule_alignment(align_errors), "16 KiB page granule alignment verified");

    std::vector<std::string> report;
    TEST_ASSERT(auditor.run_full_audit(report), "Full independent memory audit PASSED");

    std::cout << "[Stage 261.3] Independent overlap auditor verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 261.4: Allocator Free-Range Derivation & Issue A Resolution
// ----------------------------------------------------------------------------
static void test_stage261_4_allocator_free_range_derivation() {
    std::cout << "[Stage 261.4] Testing Allocator Free-Range Derivation (Issue A Resolution)...\n";

    auto map = CanonicalPhysicalMemoryMap::create_canonical_iphone15_layout();

    // Derive free ranges by subtracting all reserved intervals from RAM envelope
    auto free_ranges = map.derive_free_ranges();
    TEST_ASSERT(free_ranges.size() == 1, "Exactly 1 continuous free pool interval derived");

    const auto& pool = free_ranges[0];
    TEST_ASSERT(pool.start == 0x00094000ULL, "Dynamic Free Pool starts at 0x94000 (Frame 37)");
    TEST_ASSERT(pool.end   == 0x00800000ULL, "Dynamic Free Pool ends at 0x800000 (Frame 511)");
    TEST_ASSERT(pool.size  == 0x0076C000ULL, "Dynamic Free Pool size is 7,782,400 bytes");
    TEST_ASSERT(pool.frame_count == 475, "Dynamic Free Pool contains exactly 475 frames");

    // Critical Invariant: Total RAM = 37 Reserved Frames + 475 Free Frames = 512 Frames (8 MiB)
    TEST_ASSERT(37 + pool.frame_count == 512, "37 reserved + 475 free = 512 total frames");

    // Issue A Resolution Verification:
    // Kernel text at 0x80000 is Frame 32.
    // Dynamic Free Pool starts at 0x94000 (Frame 37).
    // Therefore: Frame 32 (0x80000) is STRICTLY EXCLUDED from the allocator free pool!
    TEST_ASSERT(0x00080000ULL < pool.start, "Kernel text 0x80000 is strictly before the allocator pool");
    TEST_ASSERT(map.is_range_reserved(0x00080000ULL, 0x4000ULL), "Frame 32 (0x80000) is marked as reserved");

    // Reserved ranges check
    TEST_ASSERT(map.is_range_reserved(0x00000000ULL, 0x4000ULL), "Frame 0 is reserved");
    TEST_ASSERT(map.is_range_reserved(0x00040000ULL, 0x4000ULL), "Frame 16 (Core 0 stack) is reserved");
    TEST_ASSERT(map.is_range_reserved(0x00048000ULL, 0x8000ULL), "Frames 18-19 (Page tables) are reserved");
    TEST_ASSERT(!map.is_range_reserved(0x00094000ULL, 0x4000ULL), "Frame 37 is unreserved (free)");
    TEST_ASSERT(!map.is_range_reserved(0x007FC000ULL, 0x4000ULL), "Frame 511 is unreserved (free)");

    std::cout << "[Stage 261.4] Allocator free-range derivation and Issue A resolved.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 261.5: Allocator Adversarial Verification
// ----------------------------------------------------------------------------
static void test_stage261_5_allocator_adversarial_verification() {
    std::cout << "[Stage 261.5] Testing Allocator Adversarial Verification...\n";

    XnuPhysicalPageAllocator alloc;
    std::string err;

    // Initialize physical page allocator with full 8 MiB RAM
    TEST_ASSERT(alloc.initialize(0x0ULL, 0x00800000ULL, &err), "Initialize 8 MiB RAM");

    // Reserve all 37 bootstrap and enclave frames (0x0 .. 0x94000)
    TEST_ASSERT(alloc.reserve_range(0x0ULL, 0x00094000ULL, "ReservedEnclave", &err), "Reserve 37 frames");
    TEST_ASSERT(alloc.reserved_pages_count() == 37, "Exactly 37 frames reserved");
    TEST_ASSERT(alloc.free_pages_count() == 475, "Exactly 475 frames free");

    // 1. Attempt allocation across reserved intervals
    for (std::size_t f = 0; f < 37; ++f) {
        TEST_ASSERT(alloc.is_frame_reserved(f), "Frame is reserved");
        TEST_ASSERT(!alloc.is_frame_free(f), "Reserved frame is not free");
    }

    // Explicit check on Frame 32 (0x80000)
    TEST_ASSERT(alloc.is_frame_reserved(32), "Frame 32 (Kernel text 0x80000) is strictly reserved");

    // 2. Allocate first valid frame: must be Frame 37 (0x94000)
    auto p1 = alloc.allocate_pages(1);
    TEST_ASSERT(p1.has_value(), "Allocate page succeeded");
    TEST_ASSERT(*p1 == 0x00094000ULL, "First allocated page is exactly Frame 37 (0x94000)");

    // 3. Allocate until exhaustion: exactly 474 more frames
    std::vector<uint64_t> allocated;
    allocated.push_back(*p1);
    for (std::size_t i = 1; i < 475; ++i) {
        auto p = alloc.allocate_pages(1);
        TEST_ASSERT(p.has_value(), "Page allocation succeeds");
        TEST_ASSERT(*p >= 0x00094000ULL && *p < 0x00800000ULL, "Allocated frame is in DynamicFreePool");
        allocated.push_back(*p);
    }

    // Verify exhaustion
    TEST_ASSERT(alloc.free_pages_count() == 0, "Free page count is 0 after 475 allocations");
    auto p_exhausted = alloc.allocate_pages(1);
    TEST_ASSERT(!p_exhausted.has_value(), "Allocation fails deterministically on exhaustion");

    // 4. Free and reallocation test
    TEST_ASSERT(alloc.free_pages(allocated[0], 1, &err), "Free first page");
    TEST_ASSERT(alloc.free_pages_count() == 1, "Free count is 1");
    auto p_realloc = alloc.allocate_pages(1);
    TEST_ASSERT(p_realloc.has_value() && *p_realloc == allocated[0], "Reallocation reclaims freed page");

    // 5. Error handling: Double-free rejection
    TEST_ASSERT(!alloc.free_pages(0x00080000ULL, 1, &err), "Cannot free reserved frame (0x80000)");
    TEST_ASSERT(!alloc.free_pages(0x00040000ULL, 1, &err), "Cannot free stack frame (0x40000)");
    TEST_ASSERT(!alloc.free_pages(0x00800000ULL, 1, &err), "Cannot free beyond RAM limit (0x800000)");

    std::cout << "[Stage 261.5] Allocator adversarial verification completed successfully.\n";
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    try {
        std::cout << "====================================================================\n";
        std::cout << "Campaign 261: Stages 261.1 - 261.5 Memory Map & Allocator Tests\n";
        std::cout << "====================================================================\n";

        test_stage261_1_2_canonical_representation();
        test_stage261_3_independent_overlap_auditor();
        test_stage261_4_allocator_free_range_derivation();
        test_stage261_5_allocator_adversarial_verification();

        std::cout << "====================================================================\n";
        std::cout << "All Stages 261.1 - 261.5 Tests PASSED Successfully!\n";
        std::cout << "====================================================================\n";
    } catch (const std::exception& e) {
        std::cerr << "FATAL EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}