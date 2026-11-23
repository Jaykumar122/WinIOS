#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "memory/canonical_physical_memory_map.h"
#include "memory/xnu_page_allocator.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::memory;

// ----------------------------------------------------------------------------
// Test 1: Stage 263.15 - Canonical Memory Map Reconciliation
// ----------------------------------------------------------------------------
static void test_stage263_15_canonical_map_reconciliation() {
    std::cout << "[Stage 263.15] Reconciling Canonical Physical Memory Map & Allocator Ranges...\n";

    auto map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();

    TEST_ASSERT(map.ram_base() == 0x00000000ULL, "RAM base is 0x0");
    TEST_ASSERT(map.ram_size() == 0x00800000ULL, "RAM size is 8 MiB (512 frames)");
    TEST_ASSERT(map.ram_end()  == 0x00800000ULL, "RAM end is 0x800000");

    auto free_ranges = map.derive_free_ranges();
    TEST_ASSERT(free_ranges.size() == 1, "Exactly one contiguous free pool range");

    const auto& pool = free_ranges[0];
    TEST_ASSERT(pool.start == 0x000B8000ULL, "Free pool starts at 0x000B8000 (Frame 46)");
    TEST_ASSERT(pool.end == 0x00800000ULL, "Free pool ends at 0x00800000 (Frame 512)");
    TEST_ASSERT(pool.size == 7634944ULL, "Free pool size is 7,634,944 bytes (7,456 KiB)");
    TEST_ASSERT(pool.frame_count == 466, "Free pool frame count is 466 frames");

    // Frame 32 (KernelTextSecondary) verification
    const auto* f32 = map.find_interval_by_address(0x00080000ULL);
    TEST_ASSERT(f32 != nullptr, "Frame 32 found");
    TEST_ASSERT(f32->kind == CanonicalMemoryKind::KernelTextSecondary, "Frame 32 is KernelTextSecondary");
    TEST_ASSERT(f32->start_address == 0x00080000ULL, "Frame 32 base is 0x80000");
    TEST_ASSERT(f32->size == 0x00004000ULL, "Frame 32 size is 16 KiB");
    TEST_ASSERT((f32->permissions & 4) != 0, "Frame 32 is executable");
    TEST_ASSERT(f32->end_address <= pool.start, "Frame 32 strictly precedes free pool");

    std::cout << "[Stage 263.15] Memory map reconciled: 46 reserved frames, 466 free frames.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 263.16 - Allocator Adversarial Testing
// ----------------------------------------------------------------------------
static void test_stage263_16_allocator_adversarial() {
    std::cout << "[Stage 263.16] Testing Allocator Adversarial Operations...\n";

    // Initialize physical page allocator over canonical 8MB memory:
    // Base: 0x00000000, Size: 512 * 16384 bytes (8 MiB)
    // Reserve frames 0..45 (46 frames, 0x0 to 0x000B8000), leaving 466 free frames.
    const uint64_t kRamBase = 0x00000000ULL;
    const uint64_t kRamSize = 512 * 16384ULL;
    const uint64_t kReservedSize = 46 * 16384ULL;
    const uint64_t kPoolBase = 0x000B8000ULL;
    const std::size_t kPoolFrames = 466;

    XnuPhysicalPageAllocator allocator;
    std::string err;
    TEST_ASSERT(allocator.initialize(kRamBase, kRamSize, &err), "Allocator initialization succeeded");
    TEST_ASSERT(allocator.reserve_range(kRamBase, kReservedSize, "reserved_system_frames", &err), "Reserve range succeeded");
    TEST_ASSERT(allocator.free_pages_count() == kPoolFrames, "Free frames count matches 466");

    // 1. Allocate first free frame
    auto frame1 = allocator.allocate_pages(1);
    TEST_ASSERT(frame1.has_value(), "First allocation succeeded");
    TEST_ASSERT(*frame1 == kPoolBase, "First frame is 0x000B8000");

    // 2. Allocate remaining frames up to exhaustion
    std::vector<uint64_t> allocated;
    allocated.push_back(*frame1);

    for (std::size_t i = 1; i < kPoolFrames; ++i) {
        auto f = allocator.allocate_pages(1);
        TEST_ASSERT(f.has_value(), ("Allocating frame " + std::to_string(i)).c_str());
        allocated.push_back(*f);
    }

    // Last frame check
    TEST_ASSERT(allocated.back() == kPoolBase + (kPoolFrames - 1) * 16384ULL,
                "Last frame matches expected end address 0x007FC000");

    // 3. Exhaustion test
    auto exhausted = allocator.allocate_pages(1);
    TEST_ASSERT(!exhausted.has_value(), "Allocation fails when exhausted");

    // 4. Double-free and foreign-free tests
    // Free the first frame:
    TEST_ASSERT(allocator.free_pages(allocated[0], 1, &err), "Freeing valid frame succeeds");
    // Double free must fail:
    TEST_ASSERT(!allocator.free_pages(allocated[0], 1, &err), "Double free correctly rejected");

    // Foreign address free (below pool, e.g. Frame 32 at 0x80000):
    TEST_ASSERT(!allocator.free_pages(0x00080000ULL, 1, &err), "Freeing reserved Frame 32 rejected");
    // Foreign address free (misaligned):
    TEST_ASSERT(!allocator.free_pages(0x000B8001ULL, 1, &err), "Freeing misaligned address rejected");
    // Foreign address free (above RAM, e.g. 0x00900000):
    TEST_ASSERT(!allocator.free_pages(0x00900000ULL, 1, &err), "Freeing out-of-bounds address rejected");

    std::cout << "[Stage 263.16] Allocator adversarial tests passed.\n";
}

int main() {
    std::cout << "=== Running Stage 263.16: Memory Map & Allocator Adversarial Tests ===\n";
    test_stage263_15_canonical_map_reconciliation();
    test_stage263_16_allocator_adversarial();
    std::cout << "=== ALL STAGE 263.16 TESTS PASSED ===\n";
    return 0;
}
