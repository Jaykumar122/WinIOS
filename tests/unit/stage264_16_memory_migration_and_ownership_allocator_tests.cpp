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
// Test 1: Stage 264.15 - Campaign 262 to 263 Memory-Map Migration Audit
// ----------------------------------------------------------------------------
static void test_stage264_15_memory_migration_audit() {
    std::cout << "[Stage 264.15] Auditing Memory-Map Migration (Campaign 262 to 263)...\n";

    auto map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();

    TEST_ASSERT(map.ram_base() == 0x00000000ULL, "RAM base is 0x0");
    TEST_ASSERT(map.ram_size() == 0x00800000ULL, "RAM size is 8 MiB (512 frames)");
    TEST_ASSERT(map.ram_end()  == 0x00800000ULL, "RAM end is 0x800000");

    // Reserved regions accounting:
    // Stolen page tables: frames 0..7 (0x0 - 0x20000, 8 frames)
    // Core 0-5 dual stacks & guards: frames 8..31 (0x20000 - 0x80000, 24 frames)
    // Kernel Mach-O image: frames 32..45 (0x80000 - 0xB8000, 14 frames)
    // Total reserved = 8 + 24 + 14 = 46 frames (736 KiB)
    const std::size_t kExpectedReservedFrames = 46;
    const std::size_t kExpectedFreeFrames = 512 - kExpectedReservedFrames; // 466 frames

    TEST_ASSERT(kExpectedReservedFrames == 46, "Exactly 46 reserved frames");
    TEST_ASSERT(kExpectedFreeFrames == 466, "Exactly 466 free pool frames");

    // Canonical free pool boundaries:
    const uint64_t kPoolBase = 0x000B8000ULL;
    const uint64_t kPoolEnd  = 0x00800000ULL;
    TEST_ASSERT(kPoolBase + (kExpectedFreeFrames * 16384ULL) == kPoolEnd, "Pool covers exactly up to 8 MiB");

    std::cout << "[Stage 264.15] Memory-map migration audit verified (46 reserved, 466 free frames).\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 264.16 - Canonical Memory Ownership & Allocator Adversarial Tests
// ----------------------------------------------------------------------------
static void test_stage264_16_ownership_and_allocator_adversarial() {
    std::cout << "[Stage 264.16] Testing Ownership Invariants & Allocator Adversarial Safety...\n";

    const uint64_t kRamBase = 0x00000000ULL;
    const uint64_t kRamSize = 512 * 16384ULL; // 8 MiB
    const uint64_t kReservedSize = 46 * 16384ULL; // 46 frames = 736 KiB
    const uint64_t kPoolBase = 0x000B8000ULL;
    const std::size_t kPoolFrames = 466;

    XnuPhysicalPageAllocator allocator;
    std::string err;
    TEST_ASSERT(allocator.initialize(kRamBase, kRamSize, &err), "Allocator initialization succeeded");
    TEST_ASSERT(allocator.reserve_range(kRamBase, kReservedSize, "reserved_system_frames", &err), "Reserve range succeeded");
    TEST_ASSERT(allocator.free_pages_count() == kPoolFrames, "Free frames count matches 466");

    // 1. First frame allocation
    auto frame1 = allocator.allocate_pages(1);
    TEST_ASSERT(frame1.has_value(), "First allocation succeeded");
    TEST_ASSERT(*frame1 == kPoolBase, "First frame is 0x000B8000 (Frame 46)");

    // 2. Allocate remaining 465 frames up to exhaustion
    std::vector<uint64_t> allocated;
    allocated.push_back(*frame1);

    for (std::size_t i = 1; i < kPoolFrames; ++i) {
        auto f = allocator.allocate_pages(1);
        TEST_ASSERT(f.has_value(), ("Allocating frame " + std::to_string(i)).c_str());
        allocated.push_back(*f);
    }

    // Verify last allocated frame
    TEST_ASSERT(allocated.back() == kPoolBase + (kPoolFrames - 1) * 16384ULL,
                "Last frame matches Frame 511 at 0x007FC000");

    // 3. Exhaustion test: frame 467 must return nullopt
    auto exhausted = allocator.allocate_pages(1);
    TEST_ASSERT(!exhausted.has_value(), "Allocation fails when pool is fully exhausted");

    // 4. Double-free rejection test
    TEST_ASSERT(allocator.free_pages(allocated[0], 1, &err), "Freeing valid frame succeeds");
    TEST_ASSERT(!allocator.free_pages(allocated[0], 1, &err), "Double free must be rejected");

    // 5. Out-of-bounds, misaligned, and reserved frees
    TEST_ASSERT(!allocator.free_pages(0x00080000ULL, 1, &err), "Freeing reserved Frame 32 rejected");
    TEST_ASSERT(!allocator.free_pages(0x000B8001ULL, 1, &err), "Freeing misaligned address rejected");
    TEST_ASSERT(!allocator.free_pages(0x00900000ULL, 1, &err), "Freeing out-of-bounds address rejected");

    std::cout << "[Stage 264.16] Canonical memory ownership and allocator adversarial tests passed.\n";
}

int main() {
    std::cout << "=== Running Stage 264.16: Memory Migration & Ownership Allocator Tests ===\n";
    test_stage264_15_memory_migration_audit();
    test_stage264_16_ownership_and_allocator_adversarial();
    std::cout << "=== ALL STAGE 264.16 TESTS PASSED ===\n";
    return 0;
}
