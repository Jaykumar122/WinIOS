#include "trace/trace_ring_buffer.h"
#include "trace/trace_event.h"
#include "trace/trace_types.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace winios::trace;

namespace {

// Helper to construct a synthetic test event with distinct sequence number
TraceEvent make_test_event(std::uint64_t seq, std::uint64_t vtime = 0, std::uint64_t pc = 0x1000) {
    TraceEvent ev{};
    ev.sequence_number = seq;
    ev.virtual_time = vtime ? vtime : seq * 10;
    ev.pc = pc;
    ev.address = 0x2000 + seq * 8;
    ev.data = 0xDEAD000000000000ULL | seq;
    ev.opcode_or_syndrome = 0xD503201F; // NOP
    ev.flags = static_cast<std::uint32_t>(TraceEventFlags::HasVirtualAddress | TraceEventFlags::HasData);
    ev.event_type = static_cast<std::uint16_t>(TraceEventType::InstructionRetired);
    ev.core_id = 0;
    ev.exception_level = static_cast<std::uint8_t>(TracePrivilegeLevel::EL1);
    ev.address_space = static_cast<std::uint8_t>(TraceAddressSpace::Virtual);
    ev.access_size = 4;
    ev.reserved = 0;
    return ev;
}

void test_1_valid_construction() {
    std::cout << "  Test 1: Construction with valid capacity... ";
    InMemoryRingBuffer rb(1024);
    assert(rb.requested_capacity() == 1024);
    assert(rb.capacity() == 1024);
    assert(rb.empty());
    assert(!rb.full());
    assert(rb.size() == 0);
    assert(rb.available() == 1024);
    std::cout << "PASS\n";
}

void test_2_zero_capacity_rejection() {
    std::cout << "  Test 2: Zero capacity rejection... ";
    bool caught = false;
    try {
        InMemoryRingBuffer rb(0);
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    assert(caught && "Expected std::invalid_argument on capacity 0");
    std::cout << "PASS\n";
}

void test_3_max_capacity_validation() {
    std::cout << "  Test 3: Maximum capacity validation... ";
    bool caught = false;
    try {
        InMemoryRingBuffer rb(MAX_RING_BUFFER_CAPACITY + 1);
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    assert(caught && "Expected std::invalid_argument on capacity > MAX");
    std::cout << "PASS\n";
}

void test_4_empty_buffer_behavior() {
    std::cout << "  Test 4: Empty buffer behavior... ";
    InMemoryRingBuffer rb(64);
    TraceEvent ev{};
    assert(rb.empty());
    assert(!rb.full());
    assert(rb.size() == 0);
    assert(rb.available() == 64);
    assert(!rb.try_pop(ev)); // Pop on empty returns false
    std::cout << "PASS\n";
}

void test_5_single_push() {
    std::cout << "  Test 5: Single push... ";
    InMemoryRingBuffer rb(64);
    TraceEvent in = make_test_event(42);
    assert(rb.try_push(in));
    assert(!rb.empty());
    assert(rb.size() == 1);
    assert(rb.available() == 63);
    assert(rb.total_accepted() == 1);
    std::cout << "PASS\n";
}

void test_6_single_pop() {
    std::cout << "  Test 6: Single pop... ";
    InMemoryRingBuffer rb(64);
    TraceEvent in = make_test_event(100);
    rb.try_push(in);

    TraceEvent out{};
    assert(rb.try_pop(out));
    assert(out == in);
    assert(rb.empty());
    assert(rb.size() == 0);
    assert(rb.total_consumed() == 1);
    std::cout << "PASS\n";
}

void test_7_fifo_ordering() {
    std::cout << "  Test 7: FIFO ordering... ";
    InMemoryRingBuffer rb(64);
    for (std::uint64_t i = 1; i <= 5; ++i) {
        assert(rb.try_push(make_test_event(i)));
    }
    for (std::uint64_t i = 1; i <= 5; ++i) {
        TraceEvent out{};
        assert(rb.try_pop(out));
        assert(out.sequence_number == i);
    }
    assert(rb.empty());
    std::cout << "PASS\n";
}

void test_8_full_buffer_detection() {
    std::cout << "  Test 8: Full buffer detection... ";
    InMemoryRingBuffer rb(8); // Power of 2: 8
    assert(rb.capacity() == 8);
    for (std::uint64_t i = 1; i <= 8; ++i) {
        assert(rb.try_push(make_test_event(i)));
    }
    assert(rb.full());
    assert(rb.size() == 8);
    assert(rb.available() == 0);
    std::cout << "PASS\n";
}

void test_9_empty_buffer_after_draining() {
    std::cout << "  Test 9: Empty buffer after draining... ";
    InMemoryRingBuffer rb(16);
    for (std::uint64_t i = 1; i <= 16; ++i) {
        rb.try_push(make_test_event(i));
    }
    assert(rb.full());
    for (std::uint64_t i = 1; i <= 16; ++i) {
        TraceEvent out{};
        assert(rb.try_pop(out));
    }
    assert(rb.empty());
    assert(rb.size() == 0);
    assert(rb.available() == 16);
    std::cout << "PASS\n";
}

void test_10_drop_newest_behavior() {
    std::cout << "  Test 10: Drop-newest overflow policy... ";
    InMemoryRingBuffer rb(4);
    for (std::uint64_t i = 1; i <= 4; ++i) {
        assert(rb.try_push(make_test_event(i)));
    }
    assert(rb.full());

    // 5th event must be rejected
    TraceEvent overflow_ev = make_test_event(5);
    assert(!rb.try_push(overflow_ev));
    std::cout << "PASS\n";
}

void test_11_dropped_counter_increment() {
    std::cout << "  Test 11: Dropped-event counter accounting... ";
    InMemoryRingBuffer rb(4);
    for (std::uint64_t i = 1; i <= 4; ++i) {
        rb.try_push(make_test_event(i));
    }
    assert(rb.dropped_events() == 0);

    // Push 3 more into full buffer
    for (int i = 0; i < 3; ++i) {
        assert(!rb.try_push(make_test_event(99)));
    }
    assert(rb.dropped_events() == 3);
    std::cout << "PASS (dropped = 3)\n";
}

void test_12_no_unread_event_overwrite() {
    std::cout << "  Test 12: No unread event overwrite... ";
    InMemoryRingBuffer rb(4);
    for (std::uint64_t i = 1; i <= 4; ++i) {
        rb.try_push(make_test_event(i));
    }
    // Attempt multiple pushes that drop
    for (int i = 0; i < 5; ++i) {
        rb.try_push(make_test_event(999));
    }
    // Pop original 4 events and assert they were NOT overwritten
    for (std::uint64_t i = 1; i <= 4; ++i) {
        TraceEvent out{};
        assert(rb.try_pop(out));
        assert(out.sequence_number == i);
    }
    assert(rb.empty());
    std::cout << "PASS\n";
}

void test_13_wraparound() {
    std::cout << "  Test 13: Wraparound across capacity boundary... ";
    InMemoryRingBuffer rb(4);
    // Push 3, pop 2 (head at 3, tail at 2)
    rb.try_push(make_test_event(1));
    rb.try_push(make_test_event(2));
    rb.try_push(make_test_event(3));
    TraceEvent out{};
    rb.try_pop(out); assert(out.sequence_number == 1);
    rb.try_pop(out); assert(out.sequence_number == 2);

    // Push 3 more (wrapping past index 4)
    assert(rb.try_push(make_test_event(4)));
    assert(rb.try_push(make_test_event(5)));
    assert(rb.try_push(make_test_event(6)));

    // Pop all remaining (3, 4, 5, 6)
    rb.try_pop(out); assert(out.sequence_number == 3);
    rb.try_pop(out); assert(out.sequence_number == 4);
    rb.try_pop(out); assert(out.sequence_number == 5);
    rb.try_pop(out); assert(out.sequence_number == 6);
    assert(rb.empty());
    std::cout << "PASS\n";
}

void test_14_repeated_wraparound() {
    std::cout << "  Test 14: Repeated wraparound (10,000 cycles)... ";
    InMemoryRingBuffer rb(8);
    for (std::uint64_t i = 0; i < 10000; ++i) {
        assert(rb.try_push(make_test_event(i)));
        TraceEvent out{};
        assert(rb.try_pop(out));
        assert(out.sequence_number == i);
    }
    assert(rb.empty());
    assert(rb.total_accepted() == 10000);
    assert(rb.total_consumed() == 10000);
    std::cout << "PASS\n";
}

void test_15_capacity_accounting() {
    std::cout << "  Test 15: Capacity accounting & power-of-two rounding... ";
    InMemoryRingBuffer rb(100); // 100 rounds up to 128
    assert(rb.requested_capacity() == 100);
    assert(rb.capacity() == 128);
    std::cout << "PASS (100 -> 128)\n";
}

void test_16_available_slot_accounting() {
    std::cout << "  Test 16: Available slot accounting... ";
    InMemoryRingBuffer rb(16);
    for (std::size_t i = 0; i < 16; ++i) {
        assert(rb.size() + rb.available() == 16);
        rb.try_push(make_test_event(i));
    }
    assert(rb.size() + rb.available() == 16);
    assert(rb.full());
    std::cout << "PASS\n";
}

void test_17_accepted_event_accounting() {
    std::cout << "  Test 17: Accepted event accounting... ";
    InMemoryRingBuffer rb(32);
    for (int i = 0; i < 20; ++i) {
        rb.try_push(make_test_event(i));
    }
    assert(rb.total_accepted() == 20);
    std::cout << "PASS (accepted = 20)\n";
}

void test_18_consumed_event_accounting() {
    std::cout << "  Test 18: Consumed event accounting... ";
    InMemoryRingBuffer rb(32);
    for (int i = 0; i < 20; ++i) {
        rb.try_push(make_test_event(i));
    }
    TraceEvent out{};
    for (int i = 0; i < 12; ++i) {
        rb.try_pop(out);
    }
    assert(rb.total_consumed() == 12);
    assert(rb.size() == 8);
    std::cout << "PASS (consumed = 12)\n";
}

void test_19_high_water_mark() {
    std::cout << "  Test 19: High-water mark tracking... ";
    InMemoryRingBuffer rb(64);
    assert(rb.high_water_mark() == 0);
    for (int i = 0; i < 25; ++i) {
        rb.try_push(make_test_event(i));
    }
    assert(rb.high_water_mark() >= 25);

    // Pop 10 (size is 15, HWM should stay >= 25)
    TraceEvent out{};
    for (int i = 0; i < 10; ++i) {
        rb.try_pop(out);
    }
    assert(rb.high_water_mark() >= 25);
    std::cout << "PASS (hwm >= 25)\n";
}

void test_20_reset_while_quiescent() {
    std::cout << "  Test 20: Reset while quiescent... ";
    InMemoryRingBuffer rb(32);
    for (int i = 0; i < 10; ++i) {
        rb.try_push(make_test_event(i));
    }
    rb.reset();
    assert(rb.empty());
    assert(rb.size() == 0);
    assert(rb.available() == 32);
    std::cout << "PASS\n";
}

void test_21_reset_clears_counters() {
    std::cout << "  Test 21: Reset clears all counters... ";
    InMemoryRingBuffer rb(4);
    for (int i = 0; i < 4; ++i) rb.try_push(make_test_event(i));
    rb.try_push(make_test_event(99)); // dropped
    TraceEvent out{};
    rb.try_pop(out);

    rb.reset();
    assert(rb.dropped_events() == 0);
    assert(rb.total_accepted() == 0);
    assert(rb.total_consumed() == 0);
    assert(rb.high_water_mark() == 0);
    std::cout << "PASS\n";
}

void test_22_reuse_after_reset() {
    std::cout << "  Test 22: Reuse after reset... ";
    InMemoryRingBuffer rb(16);
    for (int i = 0; i < 10; ++i) rb.try_push(make_test_event(i));
    rb.reset();

    for (int i = 1; i <= 5; ++i) {
        assert(rb.try_push(make_test_event(i)));
    }
    assert(rb.size() == 5);
    for (int i = 1; i <= 5; ++i) {
        TraceEvent out{};
        assert(rb.try_pop(out));
        assert(out.sequence_number == static_cast<std::uint64_t>(i));
    }
    assert(rb.empty());
    std::cout << "PASS\n";
}

void test_23_large_event_payloads() {
    std::cout << "  Test 23: Large event payload fidelity... ";
    InMemoryRingBuffer rb(16);
    TraceEvent large{};
    large.sequence_number = 0xFFFFFFFFFFFFFFFFULL;
    large.virtual_time = 0xEEEEEEEEEEEEEEEEULL;
    large.pc = 0xFFFFFF8000001000ULL;
    large.address = 0xFFFFFF8080002000ULL;
    large.data = 0x0123456789ABCDEFULL;
    large.opcode_or_syndrome = 0xCAFEBABE;
    large.flags = 0x55555555;
    large.event_type = static_cast<std::uint16_t>(TraceEventType::MemoryWrite);
    large.core_id = 0xFFFF;
    large.exception_level = 3;
    large.address_space = 2;
    large.access_size = 16;
    large.reserved = 0;

    assert(rb.try_push(large));
    TraceEvent out{};
    assert(rb.try_pop(out));
    assert(out == large);
    std::cout << "PASS\n";
}

void test_24_all_trace_event_fields_survive_copy() {
    std::cout << "  Test 24: All TraceEvent fields survive copying... ";
    InMemoryRingBuffer rb(16);
    TraceEvent in{};
    in.sequence_number    = 1001;
    in.virtual_time       = 2002;
    in.pc                 = 3003;
    in.address            = 4004;
    in.data               = 5005;
    in.opcode_or_syndrome = 6006;
    in.flags              = 7007;
    in.event_type         = 8;
    in.core_id            = 9;
    in.exception_level    = 1;
    in.address_space      = 2;
    in.access_size        = 4;
    in.reserved           = 0;

    rb.try_push(in);
    TraceEvent out{};
    rb.try_pop(out);

    assert(out.sequence_number    == 1001);
    assert(out.virtual_time       == 2002);
    assert(out.pc                 == 3003);
    assert(out.address            == 4004);
    assert(out.data               == 5005);
    assert(out.opcode_or_syndrome == 6006);
    assert(out.flags              == 7007);
    assert(out.event_type         == 8);
    assert(out.core_id            == 9);
    assert(out.exception_level    == 1);
    assert(out.address_space      == 2);
    assert(out.access_size        == 4);
    assert(out.reserved           == 0);
    std::cout << "PASS\n";
}

void test_25_to_28_concurrent_stress() {
    std::cout << "  Tests 25-28: Producer-Consumer concurrent stress (100,000 events)... ";
    constexpr std::size_t kCapacity = 131072;
    constexpr std::uint64_t kTotalEvents = 100000;
    InMemoryRingBuffer rb(kCapacity);

    std::vector<std::uint64_t> received_events;
    received_events.reserve(kTotalEvents);

    std::atomic<bool> producer_done{false};

    // Producer Thread
    std::thread producer([&]() {
        for (std::uint64_t i = 1; i <= kTotalEvents; ++i) {
            TraceEvent ev = make_test_event(i);
            while (!rb.try_push(ev)) {
                // Yield when buffer is full
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    // Consumer Thread
    std::thread consumer([&]() {
        TraceEvent ev{};
        while (!producer_done.load(std::memory_order_acquire) || !rb.empty()) {
            if (rb.try_pop(ev)) {
                received_events.push_back(ev.sequence_number);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    // Test 25: Stress completed
    assert(received_events.size() == kTotalEvents);

    // Test 26: Strict FIFO ordering
    for (std::size_t i = 0; i < received_events.size(); ++i) {
        assert(received_events[i] == i + 1);
    }

    // Test 27: No duplicate events
    // Test 28: No missing accepted events
    assert(rb.total_accepted() == kTotalEvents);
    assert(rb.total_consumed() == kTotalEvents);
    assert(rb.dropped_events() == 0);
    assert(rb.empty());
    std::cout << "PASS (100k events FIFO verified)\n";
}

void test_29_overflow_under_concurrent_load() {
    std::cout << "  Test 29: Overflow under concurrent load... ";
    constexpr std::size_t kCapacity = 64; // Small buffer to force drops
    constexpr std::uint64_t kTotalEvents = 20000;
    InMemoryRingBuffer rb(kCapacity);

    std::atomic<bool> producer_done{false};
    std::vector<std::uint64_t> received;
    received.reserve(kTotalEvents);

    // Fast Producer (Pushes without waiting)
    std::thread producer([&]() {
        for (std::uint64_t i = 1; i <= kTotalEvents; ++i) {
            rb.try_push(make_test_event(i));
        }
        producer_done.store(true, std::memory_order_release);
    });

    // Slower Consumer
    std::thread consumer([&]() {
        TraceEvent ev{};
        while (!producer_done.load(std::memory_order_acquire) || !rb.empty()) {
            if (rb.try_pop(ev)) {
                received.push_back(ev.sequence_number);
            }
        }
    });

    producer.join();
    consumer.join();

    // Verify accounting invariant: total_accepted + dropped == total_pushed
    assert(rb.total_accepted() + rb.dropped_events() == kTotalEvents);
    assert(rb.total_consumed() == received.size());
    assert(rb.total_accepted() == received.size());

    // Verify received events are strictly monotonically increasing (no reordering)
    for (std::size_t i = 1; i < received.size(); ++i) {
        assert(received[i] > received[i - 1]);
    }
    std::cout << "PASS (accepted: " << rb.total_accepted() << ", dropped: " << rb.dropped_events() << ")\n";
}

void test_30_repeated_construction_destruction() {
    std::cout << "  Test 30: Repeated construction & destruction... ";
    for (int i = 0; i < 100; ++i) {
        InMemoryRingBuffer rb(512);
        rb.try_push(make_test_event(1));
        rb.try_push(make_test_event(2));
        TraceEvent out{};
        rb.try_pop(out);
    }
    std::cout << "PASS (100 iterations)\n";
}

void test_31_batch_push_and_pop() {
    std::cout << "  Test 31: Batch push and pop APIs... ";
    InMemoryRingBuffer rb(16);
    TraceEvent batch_in[5];
    for (int i = 0; i < 5; ++i) {
        batch_in[i] = make_test_event(i + 1);
    }

    std::size_t pushed = rb.push_batch(batch_in, 5);
    if (pushed != 5) std::abort();
    assert(pushed == 5);
    assert(rb.size() == 5);

    TraceEvent batch_out[5]{};
    std::size_t popped = rb.pop_batch(batch_out, 5);
    if (popped != 5) std::abort();
    assert(popped == 5);
    assert(rb.empty());
    for (int i = 0; i < 5; ++i) {
        assert(batch_out[i].sequence_number == static_cast<std::uint64_t>(i + 1));
    }
    std::cout << "PASS\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Phase 52 Stage 3: In-Memory Ring Buffer Tests ===\n\n";

    try {
        test_1_valid_construction();
        test_2_zero_capacity_rejection();
        test_3_max_capacity_validation();
        test_4_empty_buffer_behavior();
        test_5_single_push();
        test_6_single_pop();
        test_7_fifo_ordering();
        test_8_full_buffer_detection();
        test_9_empty_buffer_after_draining();
        test_10_drop_newest_behavior();
        test_11_dropped_counter_increment();
        test_12_no_unread_event_overwrite();
        test_13_wraparound();
        test_14_repeated_wraparound();
        test_15_capacity_accounting();
        test_16_available_slot_accounting();
        test_17_accepted_event_accounting();
        test_18_consumed_event_accounting();
        test_19_high_water_mark();
        test_20_reset_while_quiescent();
        test_21_reset_clears_counters();
        test_22_reuse_after_reset();
        test_23_large_event_payloads();
        test_24_all_trace_event_fields_survive_copy();
        test_25_to_28_concurrent_stress();
        test_29_overflow_under_concurrent_load();
        test_30_repeated_construction_destruction();
        test_31_batch_push_and_pop();

        std::cout << "\n=== All 31 Phase 52 Stage 3 Tests PASSED Successfully! ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n=== Test FAILED with exception: " << e.what() << " ===\n";
        return 1;
    }
}
