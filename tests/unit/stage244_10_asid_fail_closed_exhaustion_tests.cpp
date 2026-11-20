// ============================================================================
// Stage 244.10: ASID Fail-Closed Exhaustion & Stale-Token Invalidation Tests
// Verifies ASID generation rollover, TLB flush callbacks, and strict fail-closed
// behavior when generation limit is reached, preventing stale token resurrection.
// ============================================================================

#include "memory/asid_manager.h"

#include <iostream>
#include <vector>
#include <cstdlib>
#include <cstdint>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::memory;

static void test_asid_rollover_and_exhaustion() {
    std::cout << "[Stage 244.10 - Test 1] ASID rollover and fail-closed exhaustion...\n";

    // Setup: max 3 ASIDs (1..3), max 2 generations
    AsidManager mgr(3, 2);
    TEST_ASSERT(!mgr.is_exhausted(), "Initially not exhausted");
    TEST_ASSERT(mgr.current_generation() == 1, "Initial generation is 1");

    int flush_all_count = 0;
    int flush_asid_count = 0;
    mgr.set_invalidation_callback([&](uint16_t asid, bool all_asids) {
        if (all_asids) {
            flush_all_count++;
        } else {
            (void)asid;
            flush_asid_count++;
        }
    });

    bool needs_flush = false;

    // Gen 1: Allocate all 3 slots
    auto a1 = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(a1.valid && a1.asid == 1 && a1.generation == 1, "Allocate ASID 1 Gen 1");
    TEST_ASSERT(!needs_flush, "No flush on non-exhausted allocation");

    auto a2 = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(a2.valid && a2.asid == 2 && a2.generation == 1, "Allocate ASID 2 Gen 1");

    auto a3 = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(a3.valid && a3.asid == 3 && a3.generation == 1, "Allocate ASID 3 Gen 1");
    TEST_ASSERT(mgr.active_count() == 3, "All 3 slots active");

    // All 3 tokens are valid in epoch 1
    TEST_ASSERT(mgr.is_valid_in_epoch(1, 1), "Token (1, 1) valid");
    TEST_ASSERT(mgr.is_valid_in_epoch(2, 1), "Token (2, 1) valid");
    TEST_ASSERT(mgr.is_valid_in_epoch(3, 1), "Token (3, 1) valid");

    // Pool exhaustion -> Rollover to Generation 2
    auto a4 = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(a4.valid && a4.asid == 1 && a4.generation == 2, "Allocate ASID 1 Gen 2");
    TEST_ASSERT(needs_flush, "TLB flush required on rollover");
    TEST_ASSERT(flush_all_count == 1, "Invalidation callback fired for all ASIDs");
    TEST_ASSERT(mgr.current_generation() == 2, "Generation advanced to 2");

    // Stale token check: (1, 1) MUST NOT be valid anymore!
    TEST_ASSERT(!mgr.is_valid_in_epoch(1, 1), "Stale token (1, 1) rejected");
    TEST_ASSERT(mgr.is_valid_in_epoch(1, 2), "New token (1, 2) valid");

    // Fill remaining slots in Generation 2
    auto a5 = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(a5.valid && a5.asid == 2 && a5.generation == 2, "Allocate ASID 2 Gen 2");

    auto a6 = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(a6.valid && a6.asid == 3 && a6.generation == 2, "Allocate ASID 3 Gen 2");

    // Generation 2 exhaustion -> Reaches max_generation (2) -> FAIL CLOSED!
    auto a7 = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(!a7.valid, "7th allocation must fail closed");
    TEST_ASSERT(a7.asid == 0 && a7.generation == 0, "Null allocation returned");
    TEST_ASSERT(mgr.is_exhausted(), "Manager marked permanently exhausted");
    TEST_ASSERT(flush_all_count >= 2, "Permanent flush triggered on exhaustion");

    // Even if an ASID is released, exhausted state must refuse new allocations
    bool released = mgr.release_asid(1);
    TEST_ASSERT(released, "Release succeeds");
    auto a8 = mgr.allocate_asid(needs_flush);
    TEST_ASSERT(!a8.valid, "Subsequent allocation refused after exhaustion");

    // Stale tokens cannot be resurrected
    TEST_ASSERT(!mgr.is_valid_in_epoch(1, 1), "Old token cannot be validated");
    TEST_ASSERT(!mgr.is_valid_in_epoch(1, 2), "Exhausted manager rejects all epoch queries");
}

int main() {
    std::cout << "=== Running Stage 244.10 ASID Fail-Closed Exhaustion Tests ===\n";
    test_asid_rollover_and_exhaustion();
    std::cout << "Stage 244.10: All ASID exhaustion tests passed.\n";
    return 0;
}
