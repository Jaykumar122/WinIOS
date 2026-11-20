// ============================================================================
// Stage 243.10: ASID Manager Generation, Invalidation Callbacks & Rollover Tests
// Tests ASID allocation bounds, generation rollover, overflow guards, and
// automatic invocation of TLB invalidation callbacks to preserve translation coherence.
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

static void test_asid_invalidation_callbacks() {
    std::cout << "[Stage 243.10 - Test 1] ASID invalidation callback integration...\n";
    AsidManager mgr(4); // 4 ASID slots (1..4)

    uint32_t all_flushes = 0;
    uint32_t single_flushes = 0;
    mgr.set_invalidation_callback([&](uint16_t asid, bool all_asids) {
        if (all_asids) {
            ++all_flushes;
        } else {
            ++single_flushes;
            TEST_ASSERT(asid > 0, "Single flush ASID must be non-zero");
        }
    });

    bool needs_flush = false;
    // Allocate all 4 slots
    auto a1 = mgr.allocate_asid(needs_flush);
    auto a2 = mgr.allocate_asid(needs_flush);
    auto a3 = mgr.allocate_asid(needs_flush);
    auto a4 = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(all_flushes == 0, "No global flush during normal allocations");

    // Release slot 2
    TEST_ASSERT(mgr.release_asid(a2.asid), "Release ASID 2");
    TEST_ASSERT(single_flushes == 1, "Single flush invoked on ASID retirement");

    // Re-allocate slot 2
    auto a5 = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(!needs_flush, "Re-allocation of free slot 2 does not cause rollover");
    TEST_ASSERT(a5.asid == a2.asid, "Reallocated slot 2");

    // Allocate next: now all 4 slots are in use, triggers true exhaustion and global TLB flush
    auto a6 = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(needs_flush, "Flush flag set");
    TEST_ASSERT(all_flushes == 1, "Global invalidation callback invoked on epoch rollover");
    TEST_ASSERT(a6.generation == 2, "Generation advanced to 2");
}

static void test_stale_asid_epoch_invalidation() {
    std::cout << "[Stage 243.10 - Test 2] Stale ASID rejection across generations...\n";
    AsidManager mgr(3); // 3 slots

    bool needs_flush = false;
    auto a1 = mgr.allocate_asid(needs_flush); // ASID 1, gen 1
    auto a2 = mgr.allocate_asid(needs_flush); // ASID 2, gen 1
    auto a3 = mgr.allocate_asid(needs_flush); // ASID 3, gen 1

    TEST_ASSERT(mgr.is_valid_in_epoch(a1.asid, a1.generation), "a1 valid in gen 1");

    // Force epoch advance
    auto a4 = mgr.allocate_asid(needs_flush); // Causes rollover to gen 2
    TEST_ASSERT(a4.generation == 2, "Gen advanced to 2");

    // Old token from gen 1 must now be invalid!
    TEST_ASSERT(!mgr.is_valid_in_epoch(a1.asid, 1), "Old gen 1 token rejected in gen 2");
}

int main() {
    std::cout << "=== Running Stage 243.10 ASID Generation & Invalidation Tests ===\n";
    test_asid_invalidation_callbacks();
    test_stale_asid_epoch_invalidation();
    std::cout << "Stage 243.10: All tests passed successfully.\n";
    return 0;
}
