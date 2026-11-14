#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>

#include "cpu/guest_memory_order.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;

// Test 1: Program order sequence generation and per-core event tracking
static bool test_program_order_and_histories() {
    GuestMemoryOrderModel model(4);
    model.reset();

    // Core 0 executes W(0x1000, 42), W(0x1008, 99)
    auto w1 = model.record_write(0, 0x1000, 8, 42, MemoryAccessType::NormalWrite);
    auto w2 = model.record_write(0, 0x1008, 8, 99, MemoryAccessType::NormalWrite);

    TEST_ASSERT(w1.program_order_seq == 1, "Core 0 w1 po_seq == 1");
    TEST_ASSERT(w2.program_order_seq == 2, "Core 0 w2 po_seq == 2");
    TEST_ASSERT(w1.global_commit_seq == 1, "w1 global_seq == 1");
    TEST_ASSERT(w2.global_commit_seq == 2, "w2 global_seq == 2");

    // Core 1 executes R(0x1008, 99)
    auto r1 = model.record_read(1, 0x1008, 8, 99, MemoryAccessType::NormalRead);
    TEST_ASSERT(r1.program_order_seq == 1, "Core 1 r1 po_seq == 1");
    TEST_ASSERT(r1.global_commit_seq == 3, "r1 global_seq == 3");

    // Check core histories
    const auto& c0_hist = model.get_core_state(0).event_history();
    TEST_ASSERT(c0_hist.size() == 2, "Core 0 recorded 2 events");
    const auto& c1_hist = model.get_core_state(1).event_history();
    TEST_ASSERT(c1_hist.size() == 1, "Core 1 recorded 1 event");

    TEST_ASSERT(model.total_events() == 3, "Global event log has 3 events");
    return true;
}

// Test 2: Store buffer queueing and store-to-load forwarding
static bool test_store_buffer_and_forwarding() {
    CoreOrderingState state(0);
    state.reset();

    // Queue two stores: [0x2000] = 0xAA, [0x2008] = 0xBB
    state.queue_store(1, 0x2000, 8, 0xAA, MemoryAccessType::NormalWrite);
    state.queue_store(2, 0x2008, 8, 0xBB, MemoryAccessType::NormalWrite);

    TEST_ASSERT(state.has_pending_stores(), "Store buffer has pending entries");
    TEST_ASSERT(state.pending_store_count() == 2, "Pending store count == 2");

    // Store-to-load forwarding check
    auto val1 = state.forward_store_value(0x2000, 8);
    TEST_ASSERT(val1.has_value() && *val1 == 0xAA, "Forwarded store value for 0x2000 == 0xAA");

    auto val2 = state.forward_store_value(0x2008, 8);
    TEST_ASSERT(val2.has_value() && *val2 == 0xBB, "Forwarded store value for 0x2008 == 0xBB");

    auto val3 = state.forward_store_value(0x3000, 8);
    TEST_ASSERT(!val3.has_value(), "Non-existent address returns nullopt");

    // Pop oldest store
    auto oldest = state.pop_oldest_store();
    TEST_ASSERT(oldest.has_value() && oldest->address == 0x2000, "Popped oldest store (0x2000)");
    TEST_ASSERT(state.pending_store_count() == 1, "Remaining count == 1");

    return true;
}

// Test 3: Release-Acquire Ordering Enforcement
static bool test_release_acquire_ordering() {
    GuestMemoryOrderModel model(2);
    model.reset();

    // Core 0: Normal write data, then Release write flag
    auto w_data = model.record_write(0, 0x8000, 8, 0x1234, MemoryAccessType::NormalWrite);
    auto w_flag = model.record_write(0, 0x8008, 8, 1, MemoryAccessType::ReleaseWrite);

    // Release write MUST order prior writes on same core
    TEST_ASSERT(model.is_ordered(w_data, w_flag), "Normal write is ordered before Release write");

    // Core 1: Acquire read flag, then Normal read data
    auto r_flag = model.record_read(1, 0x8008, 8, 1, MemoryAccessType::AcquireRead);
    auto r_data = model.record_read(1, 0x8000, 8, 0x1234, MemoryAccessType::NormalRead);

    // Acquire read MUST order subsequent reads on same core
    TEST_ASSERT(model.is_ordered(r_flag, r_data), "Acquire read is ordered before subsequent read");

    // Inter-core Release-Acquire ordering
    TEST_ASSERT(model.is_ordered(w_flag, r_flag), "Release write orders Acquire read across cores");

    return true;
}

// Test 4: Barrier Ordering Decisions (DMB, DSB, ISB)
static bool test_barrier_ordering() {
    GuestMemoryOrderModel model(2);
    model.reset();

    // Two normal writes on Core 0 separated by DMB ISHST (crm = 10)
    auto w1 = model.record_write(0, 0x4000, 8, 1, MemoryAccessType::NormalWrite);
    auto dmb = model.record_dmb(0, 10); // DMB ISHST
    auto w2 = model.record_write(0, 0x4008, 8, 2, MemoryAccessType::NormalWrite);

    TEST_ASSERT(model.is_ordered(w1, w2), "DMB ISHST orders w1 before w2");

    // DSB completion on Core 1
    auto r1 = model.record_read(1, 0x5000, 8, 10, MemoryAccessType::NormalRead);
    auto dsb = model.record_dsb(1, 15); // DSB SY
    auto r2 = model.record_read(1, 0x5008, 8, 20, MemoryAccessType::NormalRead);

    TEST_ASSERT(model.is_ordered(r1, r2), "DSB SY orders r1 before r2");

    // ISB flushes pipeline and records sync event
    auto isb = model.record_isb(1, 15);
    TEST_ASSERT(isb.barrier == BarrierKind::ISB, "ISB event recorded");
    TEST_ASSERT(model.get_core_state(1).pipeline_flush_count() == 1, "Pipeline flush count incremented");

    return true;
}

// Test 5: Litmus outcome evaluations (Allowed vs Forbidden)
static bool test_litmus_outcome_verdicts() {
    // Store Buffering: r0=0, r1=0 is ALLOWED under ARMv8-A weak memory
    TEST_ASSERT(GuestMemoryOrderModel::evaluate_sb_outcome(0, 0) == LitmusOutcomeVerdict::Allowed,
                "SB (0,0) is allowed");
    TEST_ASSERT(GuestMemoryOrderModel::evaluate_sb_outcome(1, 0) == LitmusOutcomeVerdict::Allowed,
                "SB (1,0) is allowed");

    // Message Passing: with release-acquire, flag=1 with data=0 is FORBIDDEN
    TEST_ASSERT(GuestMemoryOrderModel::evaluate_mp_outcome(true, 1, 0) == LitmusOutcomeVerdict::Forbidden,
                "MP (flag=1, data=0) is forbidden with release-acquire");
    TEST_ASSERT(GuestMemoryOrderModel::evaluate_mp_outcome(true, 1, 42) == LitmusOutcomeVerdict::Allowed,
                "MP (flag=1, data=42) is allowed with release-acquire");
    TEST_ASSERT(GuestMemoryOrderModel::evaluate_mp_outcome(false, 1, 0) == LitmusOutcomeVerdict::Allowed,
                "MP (flag=1, data=0) is allowed without barriers/release-acquire");

    // Load Buffering: (1,1) is forbidden by causality
    TEST_ASSERT(GuestMemoryOrderModel::evaluate_lb_outcome(1, 1) == LitmusOutcomeVerdict::Forbidden,
                "LB (1,1) is forbidden");
    TEST_ASSERT(GuestMemoryOrderModel::evaluate_lb_outcome(0, 1) == LitmusOutcomeVerdict::Allowed,
                "LB (0,1) is allowed");

    // IRIW: Cores 2 and 3 seeing contradictory orders of two independent stores is forbidden
    TEST_ASSERT(GuestMemoryOrderModel::evaluate_iriw_outcome(1, 0, 1, 0) == LitmusOutcomeVerdict::Forbidden,
                "IRIW contradictory observation is forbidden under multi-copy atomicity");

    return true;
}

// Test 6: Deterministic Ordering State Hash
static bool test_deterministic_hash() {
    GuestMemoryOrderModel model1(2);
    model1.reset();
    model1.record_write(0, 0x1000, 8, 0xAA, MemoryAccessType::NormalWrite);
    model1.record_dmb(0, 15);
    model1.record_write(0, 0x1008, 8, 0xBB, MemoryAccessType::NormalWrite);
    model1.record_read(1, 0x1008, 8, 0xBB, MemoryAccessType::NormalRead);
    std::uint64_t hash1 = model1.compute_ordering_state_hash();

    GuestMemoryOrderModel model2(2);
    model2.reset();
    model2.record_write(0, 0x1000, 8, 0xAA, MemoryAccessType::NormalWrite);
    model2.record_dmb(0, 15);
    model2.record_write(0, 0x1008, 8, 0xBB, MemoryAccessType::NormalWrite);
    model2.record_read(1, 0x1008, 8, 0xBB, MemoryAccessType::NormalRead);
    std::uint64_t hash2 = model2.compute_ordering_state_hash();

    TEST_ASSERT(hash1 == hash2, "Identical execution sequences produce identical ordering state hash");
    TEST_ASSERT(hash1 != 0, "Hash is non-zero");

    return true;
}

int main() {
    std::cout << "[STAGE 197.2] Running Guest Memory Ordering Architecture Tests...\n";
    if (!test_program_order_and_histories()) return 1;
    if (!test_store_buffer_and_forwarding()) return 2;
    if (!test_release_acquire_ordering()) return 3;
    if (!test_barrier_ordering()) return 4;
    if (!test_litmus_outcome_verdicts()) return 5;
    if (!test_deterministic_hash()) return 6;
    std::cout << "[STAGE 197.2] All Guest Memory Ordering Architecture Tests PASSED.\n";
    return 0;
}
