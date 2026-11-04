#include "trace/trace_manager.h"
#include "trace/trace_event.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace winios::trace;

namespace {

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at line " << __LINE__ << "\n"; \
        std::abort(); \
    } \
} while(0)

TraceEvent make_test_event(std::uint64_t seq, TraceEventType type = TraceEventType::InstructionRetired, std::uint16_t core = 0) {
    TraceEvent ev{};
    ev.sequence_number = seq;
    ev.virtual_time = seq * 10;
    ev.pc = 0x4000 + (seq & 0xFF);
    ev.address = 0x8000 + seq * 8;
    ev.data = 0xDEADBEEF00000000ULL | seq;
    ev.opcode_or_syndrome = 0xD503201F;
    ev.flags = static_cast<std::uint32_t>(TraceEventFlags::HasVirtualAddress | TraceEventFlags::HasData);
    ev.event_type = static_cast<std::uint16_t>(type);
    ev.core_id = core;
    ev.exception_level = 1;
    ev.address_space = 1;
    ev.access_size = 4;
    ev.reserved = 0;
    return ev;
}

void test_1_start_before_configure() {
    std::cout << "  Test 1: Start before configure rejection... ";
    TraceManager mgr;
    TEST_ASSERT(mgr.state() == TraceManagerState::Unconfigured);
    TEST_ASSERT(!mgr.start());
    TEST_ASSERT(mgr.state() == TraceManagerState::Unconfigured);
    std::cout << "PASS\n";
}

void test_2_configure_and_start() {
    std::cout << "  Test 2: Configure and start... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.state() == TraceManagerState::Configured);
    TEST_ASSERT(mgr.start());
    TEST_ASSERT(mgr.state() == TraceManagerState::Running);
    TEST_ASSERT(mgr.stop());
    TEST_ASSERT(mgr.state() == TraceManagerState::Stopped);
    std::cout << "PASS\n";
}

void test_3_submit_while_not_running() {
    std::cout << "  Test 3: Submit while not running rejected... ";
    TraceManager mgr;
    // Unconfigured submit
    TEST_ASSERT(!mgr.submit(make_test_event(1)));
    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_submitted == 1);
    TEST_ASSERT(stats.events_rejected == 1);
    TEST_ASSERT(stats.events_accepted == 0);

    // Configured but not started submit
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(!mgr.submit(make_test_event(2)));
    stats = mgr.statistics();
    TEST_ASSERT(stats.events_submitted == 2);
    TEST_ASSERT(stats.events_rejected == 2);
    std::cout << "PASS\n";
}

void test_4_submit_while_running() {
    std::cout << "  Test 4: Submit while running accepted... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    for (std::uint64_t i = 1; i <= 10; ++i) {
        TEST_ASSERT(mgr.submit(make_test_event(i)));
    }

    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_submitted == 10);
    TEST_ASSERT(stats.events_accepted == 10);
    TEST_ASSERT(stats.events_rejected == 0);
    TEST_ASSERT(stats.events_filtered == 0);
    TEST_ASSERT(stats.events_dropped == 0);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_5_reconfigure_while_running_rejected() {
    std::cout << "  Test 5: Reconfigure while running rejected... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    // Attempt configure while running
    TraceConfig cfg2 = TraceConfig::default_enabled_config();
    cfg2.ring_capacity = 128;
    TEST_ASSERT(!mgr.configure(cfg2));

    TEST_ASSERT(mgr.stop());
    // After stop, configure must succeed
    TEST_ASSERT(mgr.configure(cfg2));
    TEST_ASSERT(mgr.config().ring_capacity == 128);
    std::cout << "PASS\n";
}

void test_6_repeated_stop_is_safe() {
    std::cout << "  Test 6: Repeated stop is safe and idempotent... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    TEST_ASSERT(mgr.stop());
    TEST_ASSERT(mgr.state() == TraceManagerState::Stopped);
    TEST_ASSERT(mgr.stop()); // Second call
    TEST_ASSERT(mgr.state() == TraceManagerState::Stopped);
    std::cout << "PASS\n";
}

void test_7_event_type_filtering() {
    std::cout << "  Test 7: Event type filtering... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    // Only allow InstructionRetired; disable BranchTaken
    cfg.event_types.reset();
    cfg.event_types.set(TraceEventType::InstructionRetired, true);

    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    // Submit allowed type
    TEST_ASSERT(mgr.submit(make_test_event(1, TraceEventType::InstructionRetired)));
    // Submit disallowed type
    TEST_ASSERT(!mgr.submit(make_test_event(2, TraceEventType::BranchTaken)));
    TEST_ASSERT(!mgr.submit(make_test_event(3, TraceEventType::MemoryRead)));

    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_submitted == 3);
    TEST_ASSERT(stats.events_accepted == 1);
    TEST_ASSERT(stats.events_filtered == 2);
    TEST_ASSERT(stats.events_dropped == 0);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_8_core_id_filtering() {
    std::cout << "  Test 8: Core ID filtering... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 64;
    // Only allow Core 0 and Core 2 (mask = (1 << 0) | (1 << 2) = 0x5)
    cfg.core_mask = 0x5ULL;

    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    // Core 0 (allowed)
    TEST_ASSERT(mgr.submit(make_test_event(1, TraceEventType::InstructionRetired, 0)));
    // Core 1 (disallowed)
    TEST_ASSERT(!mgr.submit(make_test_event(2, TraceEventType::InstructionRetired, 1)));
    // Core 2 (allowed)
    TEST_ASSERT(mgr.submit(make_test_event(3, TraceEventType::InstructionRetired, 2)));
    // Core 3 (disallowed)
    TEST_ASSERT(!mgr.submit(make_test_event(4, TraceEventType::InstructionRetired, 3)));

    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_submitted == 4);
    TEST_ASSERT(stats.events_accepted == 2);
    TEST_ASSERT(stats.events_filtered == 2);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_9_ring_buffer_overflow_drop_newest() {
    std::cout << "  Test 9: Ring buffer overflow drop-newest policy... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    // Capacity 8
    cfg.ring_capacity = 8;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    // Push 8 events (fills buffer)
    for (std::uint64_t i = 1; i <= 8; ++i) {
        TEST_ASSERT(mgr.submit(make_test_event(i)));
    }

    // Next 4 events must be dropped due to capacity
    for (std::uint64_t i = 9; i <= 12; ++i) {
        TEST_ASSERT(!mgr.submit(make_test_event(i)));
    }

    auto stats = mgr.statistics();
    TEST_ASSERT(stats.events_submitted == 12);
    TEST_ASSERT(stats.events_accepted == 8);
    TEST_ASSERT(stats.events_dropped == 4);
    TEST_ASSERT(stats.ring_high_water_mark == 8);

    // Verify conservation: submitted == accepted + filtered + rejected + dropped
    TEST_ASSERT(stats.events_submitted == stats.events_accepted + stats.events_filtered + stats.events_rejected + stats.events_dropped);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

void test_10_high_water_mark_reporting() {
    std::cout << "  Test 10: High-water mark reporting... ";
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 32;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    for (std::uint64_t i = 1; i <= 20; ++i) {
        TEST_ASSERT(mgr.submit(make_test_event(i)));
    }

    auto stats = mgr.statistics();
    TEST_ASSERT(stats.ring_high_water_mark == 20);
    TEST_ASSERT(stats.ring_current_size == 20);
    TEST_ASSERT(stats.ring_capacity == 32);

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Phase 52 Stage 5: Trace Manager Unit Tests ===\n\n";

    test_1_start_before_configure();
    test_2_configure_and_start();
    test_3_submit_while_not_running();
    test_4_submit_while_running();
    test_5_reconfigure_while_running_rejected();
    test_6_repeated_stop_is_safe();
    test_7_event_type_filtering();
    test_8_core_id_filtering();
    test_9_ring_buffer_overflow_drop_newest();
    test_10_high_water_mark_reporting();

    std::cout << "\n=== All 10 Trace Manager Unit Tests PASSED Successfully! ===\n";
    return 0;
}
