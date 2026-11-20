// ============================================================================
// Stage 242.5: Emulated ASID Lifecycle, Allocation, Exhaustion & Reuse Tests
// Exercises ASID pool allocation, exhaustion, generation tracking, retirement,
// and verification that address-space identity is not confused after reuse.
// ============================================================================

#include "memory/asid_manager.h"
#include "cpu/mmu.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::memory;
using namespace mmu;

static void test_asid_allocation_and_release() {
    std::cout << "[Stage 242.5 - Test 1] Basic ASID allocation, retirement, and validation...\n";
    AsidManager mgr(10); // Small ASID pool: max ASID = 10 (ASIDs 1..10)

    bool needs_flush = false;
    auto alloc1 = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(alloc1.valid && alloc1.asid == 1 && alloc1.generation == 1, "Alloc 1");
    TEST_ASSERT(!needs_flush, "No flush needed initially");

    auto alloc2 = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(alloc2.valid && alloc2.asid == 2 && alloc2.generation == 1, "Alloc 2");
    TEST_ASSERT(!needs_flush, "No flush needed");

    TEST_ASSERT(mgr.is_valid_in_epoch(1, 1), "ASID 1 valid in gen 1");
    TEST_ASSERT(mgr.is_valid_in_epoch(2, 1), "ASID 2 valid in gen 1");
    TEST_ASSERT(!mgr.is_valid_in_epoch(3, 1), "ASID 3 not yet allocated");

    // Release ASID 1
    TEST_ASSERT(mgr.release_asid(1), "Release ASID 1");
    TEST_ASSERT(!mgr.is_valid_in_epoch(1, 1), "ASID 1 no longer valid");
    TEST_ASSERT(!mgr.release_asid(1), "Duplicate release must fail");
    TEST_ASSERT(!mgr.release_asid(0), "Release reserved ASID 0 must fail");
}

static void test_asid_exhaustion_and_generation_rollover() {
    std::cout << "[Stage 242.5 - Test 2] ASID exhaustion, epoch generation increment, and TLB flush trigger...\n";
    const uint16_t pool_size = 5;
    AsidManager mgr(pool_size); // Slots 1..5

    std::vector<AsidManager::AsidAllocation> allocs;
    bool needs_flush = false;

    // Allocate all 5 slots in generation 1
    for (uint16_t i = 1; i <= pool_size; ++i) {
        auto a = mgr.allocate_asid(needs_flush);
        TEST_ASSERT(a.valid && a.generation == 1 && !needs_flush, "Alloc within gen 1");
        allocs.push_back(a);
    }
    TEST_ASSERT(mgr.active_count() == 5, "All 5 slots active");

    // 6th allocation must trigger exhaustion, generation increment, and TLB flush signal
    auto overflow_alloc = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(overflow_alloc.valid, "Alloc succeeds after rollover");
    TEST_ASSERT(overflow_alloc.generation == 2, "Generation increments to 2");
    TEST_ASSERT(needs_flush, "TLB flush signal must be set on epoch transition");

    // Old allocation with gen 1 should no longer be valid in gen 2 epoch
    TEST_ASSERT(!mgr.is_valid_in_epoch(overflow_alloc.asid, 1), "Old gen 1 invalidated in new epoch");
    TEST_ASSERT(mgr.is_valid_in_epoch(overflow_alloc.asid, 2), "Valid in gen 2");
}

static void test_asid_tlb_invalidation_integration() {
    std::cout << "[Stage 242.5 - Test 3] Emulated ASID and software TLB invalidation integration...\n";
    MMU mmu_inst;
    mmu_inst.reset();
    TEST_ASSERT(mmu_inst.is_tlb_enabled(), "TLB enabled by default");

    // Invalidate by ASID
    const uint16_t target_asid = 42;
    mmu_inst.invalidate_tlb_asid(target_asid);
    TEST_ASSERT(mmu_inst.get_tlb_invalidations() >= 1, "TLB invalidation count increments");

    // Invalidate all
    mmu_inst.invalidate_tlb_all();
    TEST_ASSERT(mmu_inst.get_tlb_invalidations() >= 2, "TLB invalidate all increments");
}

int main() {
    std::cout << "=== Running Stage 242.5 ASID Lifecycle Tests ===\n";
    test_asid_allocation_and_release();
    test_asid_exhaustion_and_generation_rollover();
    test_asid_tlb_invalidation_integration();
    std::cout << "Stage 242.5: All tests passed successfully.\n";
    return 0;
}
