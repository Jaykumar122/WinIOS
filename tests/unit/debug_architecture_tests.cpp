#include "cpu/debug_architecture.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

// Phase 51 Stage 2: Debug Architecture Foundation Tests
// Tests the per-core debug state, register access, matching logic, and
// thread-safety of the debug architecture subsystem.

namespace {

void test_default_state() {
    std::cout << "Test: Default state\n";
    winios::DebugArchitecture debug;

    // MDSCR_EL1 defaults to 0 (monitor debug disabled).
    assert(debug.get_mdscr_el1() == 0);
    assert(!debug.monitor_debug_enabled());

    // All breakpoints default to disabled (DBGBCR.E = 0).
    for (std::size_t i = 0; i < winios::kDebugBreakpointCount; ++i) {
        assert(debug.get_dbgbvr(i) == 0);
        assert(debug.get_dbgbcr(i) == 0);
    }

    // All watchpoints default to disabled (DBGWCR.E = 0).
    for (std::size_t i = 0; i < winios::kDebugWatchpointCount; ++i) {
        assert(debug.get_dbgwvr(i) == 0);
        assert(debug.get_dbgwcr(i) == 0);
    }

    // No breakpoint or watchpoint should match in default state.
    auto bp_event = debug.check_execution_breakpoint(0x1000, 1);
    assert(!bp_event.has_value());

    auto wp_event = debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1);
    assert(!wp_event.has_value());

    std::cout << "  PASS\n";
}

void test_reset_behavior() {
    std::cout << "Test: Reset behavior\n";
    winios::DebugArchitecture debug;

    // Configure some debug state.
    debug.set_mdscr_el1(0x8000); // Enable MDE
    debug.set_dbgbvr(0, 0x1000);
    debug.set_dbgbcr(0, 0x1); // Enable breakpoint 0
    debug.set_dbgwvr(1, 0x2000);
    debug.set_dbgwcr(1, 0xFF01); // Enable watchpoint 1, BAS=0xFF

    // Verify configured state.
    assert(debug.monitor_debug_enabled());
    assert(debug.get_dbgbcr(0) == 0x1);
    assert(debug.get_dbgwcr(1) == 0xFF01);

    // Reset.
    debug.reset();

    // Verify reset state.
    assert(debug.get_mdscr_el1() == 0);
    assert(!debug.monitor_debug_enabled());
    for (std::size_t i = 0; i < winios::kDebugBreakpointCount; ++i) {
        assert(debug.get_dbgbvr(i) == 0);
        assert(debug.get_dbgbcr(i) == 0);
    }
    for (std::size_t i = 0; i < winios::kDebugWatchpointCount; ++i) {
        assert(debug.get_dbgwvr(i) == 0);
        assert(debug.get_dbgwcr(i) == 0);
    }

    std::cout << "  PASS\n";
}

void test_four_slot_limits() {
    std::cout << "Test: Four-slot limits\n";
    winios::DebugArchitecture debug;

    // Valid accesses: indices 0-3.
    for (std::size_t i = 0; i < 4; ++i) {
        debug.set_dbgbvr(i, 0x1000 + i * 0x10);
        debug.set_dbgbcr(i, 0x1);
        assert(debug.get_dbgbvr(i) == 0x1000 + i * 0x10);
        assert(debug.get_dbgbcr(i) == 0x1);

        debug.set_dbgwvr(i, 0x2000 + i * 0x10);
        debug.set_dbgwcr(i, 0xFF01);
        assert(debug.get_dbgwvr(i) == 0x2000 + i * 0x10);
        assert(debug.get_dbgwcr(i) == 0xFF01);
    }

    // Invalid access: index 4 should throw.
    bool threw_bp = false;
    try {
        debug.get_dbgbvr(4);
    } catch (const std::out_of_range&) {
        threw_bp = true;
    }
    assert(threw_bp);

    bool threw_wp = false;
    try {
        debug.set_dbgwcr(4, 0x1);
    } catch (const std::out_of_range&) {
        threw_wp = true;
    }
    assert(threw_wp);

    // Verify capability reporting.
    assert(winios::DebugArchitecture::breakpoint_count_minus_one() == 3);
    assert(winios::DebugArchitecture::watchpoint_count_minus_one() == 3);

    std::cout << "  PASS\n";
}

void test_independent_state_across_cores() {
    std::cout << "Test: Independent state across four CPU cores\n";

    // Simulate four independent cores.
    std::vector<winios::DebugArchitecture> cores(4);

    // Configure each core with unique debug state.
    for (std::size_t core = 0; core < 4; ++core) {
        cores[core].set_mdscr_el1(1ULL << 15); // MDE enabled
        cores[core].set_dbgbvr(0, 0x1000 * (core + 1));
        cores[core].set_dbgbcr(0, 0x1 | static_cast<std::uint32_t>(core << 8));
        cores[core].set_dbgwvr(1, 0x2000 * (core + 1));
        cores[core].set_dbgwcr(1, 0xFF01 | static_cast<std::uint32_t>(core << 16));
    }

    // Verify each core's state is independent.
    for (std::size_t core = 0; core < 4; ++core) {
        assert(cores[core].monitor_debug_enabled());
        assert(cores[core].get_mdscr_el1() == (1ULL << 15));
        assert(cores[core].get_dbgbvr(0) == 0x1000 * (core + 1));
        assert(cores[core].get_dbgbcr(0) == (0x1 | static_cast<std::uint32_t>(core << 8)));
        assert(cores[core].get_dbgwvr(1) == 0x2000 * (core + 1));
        assert(cores[core].get_dbgwcr(1) == (0xFF01 | static_cast<std::uint32_t>(core << 16)));
    }

    // Verify no cross-core interference.
    cores[0].set_dbgbvr(2, 0xDEADBEEF);
    assert(cores[0].get_dbgbvr(2) == 0xDEADBEEF);
    for (std::size_t core = 1; core < 4; ++core) {
        assert(cores[core].get_dbgbvr(2) == 0);
    }

    std::cout << "  PASS\n";
}

void test_breakpoint_configuration() {
    std::cout << "Test: Breakpoint configuration and retrieval\n";
    winios::DebugArchitecture debug;

    // Configure breakpoint 1.
    debug.set_dbgbvr(1, 0x123456789ABCDEF0ULL);
    debug.set_dbgbcr(1, 0x00F00F07); // Arbitrary bit pattern

    assert(debug.get_dbgbvr(1) == 0x123456789ABCDEF0ULL);
    assert(debug.get_dbgbcr(1) == 0x00F00F07);

    // Configure all four breakpoints with distinct values.
    for (std::size_t i = 0; i < 4; ++i) {
        debug.set_dbgbvr(i, 0xAAAA0000ULL + i);
        debug.set_dbgbcr(i, 0x10000000u + static_cast<std::uint32_t>(i));
    }
    for (std::size_t i = 0; i < 4; ++i) {
        assert(debug.get_dbgbvr(i) == 0xAAAA0000ULL + i);
        assert(debug.get_dbgbcr(i) == 0x10000000u + static_cast<std::uint32_t>(i));
    }

    std::cout << "  PASS\n";
}

void test_watchpoint_configuration() {
    std::cout << "Test: Watchpoint configuration and retrieval\n";
    winios::DebugArchitecture debug;

    // Configure watchpoint 2.
    debug.set_dbgwvr(2, 0xFEDCBA9876543210ULL);
    debug.set_dbgwcr(2, 0x1FFF1FFF); // Arbitrary bit pattern

    assert(debug.get_dbgwvr(2) == 0xFEDCBA9876543210ULL);
    assert(debug.get_dbgwcr(2) == 0x1FFF1FFF);

    // Configure all four watchpoints with distinct values.
    for (std::size_t i = 0; i < 4; ++i) {
        debug.set_dbgwvr(i, 0xBBBB0000ULL + i * 0x1000);
        debug.set_dbgwcr(i, 0x20000000u + static_cast<std::uint32_t>(i * 0x100));
    }
    for (std::size_t i = 0; i < 4; ++i) {
        assert(debug.get_dbgwvr(i) == 0xBBBB0000ULL + i * 0x1000);
        assert(debug.get_dbgwcr(i) == 0x20000000u + static_cast<std::uint32_t>(i * 0x100));
    }

    std::cout << "  PASS\n";
}

void test_mdscr_enable_disable() {
    std::cout << "Test: MDSCR_EL1 enable/disable behavior\n";
    winios::DebugArchitecture debug;

    // Default: MDE disabled.
    assert(!debug.monitor_debug_enabled());

    // Enable MDE (bit 15).
    debug.set_mdscr_el1(1ULL << 15);
    assert(debug.monitor_debug_enabled());
    assert(debug.get_mdscr_el1() == (1ULL << 15));

    // Disable MDE.
    debug.set_mdscr_el1(0);
    assert(!debug.monitor_debug_enabled());

    // Verify unsupported bits are masked (MDE bit 15 and SS bit 0 supported).
    debug.set_mdscr_el1(0xFFFFFFFFFFFFFFFFULL);
    assert(debug.get_mdscr_el1() == ((1ULL << 15) | (1ULL << 0))); // Supported bits preserved
    assert(debug.monitor_debug_enabled());

    std::cout << "  PASS\n";
}

void test_breakpoint_matching_exact() {
    std::cout << "Test: Exact breakpoint matching\n";
    winios::DebugArchitecture debug;

    // Enable monitor debug.
    debug.set_mdscr_el1(1ULL << 15);

    // Configure breakpoint 0 at VA 0x1000, enabled, no privilege filtering.
    debug.set_dbgbvr(0, 0x1000);
    debug.set_dbgbcr(0, 0x1); // E=1, PMC=0 (no filtering), BT=0

    // Exact match at 0x1000, EL1.
    auto event = debug.check_execution_breakpoint(0x1000, 1);
    assert(event.has_value());
    assert(event->type == winios::DebugEventType::Breakpoint);
    assert(event->address == 0x1000);
    assert(event->slot_index == 0);

    // Non-matching address.
    auto event2 = debug.check_execution_breakpoint(0x1004, 1);
    assert(!event2.has_value());

    // Exact match at EL0.
    auto event3 = debug.check_execution_breakpoint(0x1000, 0);
    assert(event3.has_value());
    assert(event3->slot_index == 0);

    std::cout << "  PASS\n";
}

void test_breakpoint_disabled() {
    std::cout << "Test: Disabled breakpoint does not match\n";
    winios::DebugArchitecture debug;

    // Enable monitor debug.
    debug.set_mdscr_el1(1ULL << 15);

    // Configure breakpoint 1 at VA 0x2000, but disabled (E=0).
    debug.set_dbgbvr(1, 0x2000);
    debug.set_dbgbcr(1, 0x0); // E=0

    // Should not match.
    auto event = debug.check_execution_breakpoint(0x2000, 1);
    assert(!event.has_value());

    // Enable the breakpoint.
    debug.set_dbgbcr(1, 0x1); // E=1
    auto event2 = debug.check_execution_breakpoint(0x2000, 1);
    assert(event2.has_value());
    assert(event2->slot_index == 1);

    std::cout << "  PASS\n";
}

void test_breakpoint_privilege_filtering() {
    std::cout << "Test: Breakpoint privilege filtering\n";
    winios::DebugArchitecture debug;

    debug.set_mdscr_el1(1ULL << 15);

    // Breakpoint 0: match only EL1 (PMC=1).
    debug.set_dbgbvr(0, 0x1000);
    debug.set_dbgbcr(0, 0x1 | (1u << 1)); // E=1, PMC=1 (EL1 only)

    // Should match at EL1.
    auto event_el1 = debug.check_execution_breakpoint(0x1000, 1);
    assert(event_el1.has_value());

    // Should not match at EL0.
    auto event_el0 = debug.check_execution_breakpoint(0x1000, 0);
    assert(!event_el0.has_value());

    // Breakpoint 1: match only EL0 (PMC=2).
    debug.set_dbgbvr(1, 0x2000);
    debug.set_dbgbcr(1, 0x1 | (2u << 1)); // E=1, PMC=2 (EL0 only)

    auto event2_el0 = debug.check_execution_breakpoint(0x2000, 0);
    assert(event2_el0.has_value());
    assert(event2_el0->slot_index == 1);

    auto event2_el1 = debug.check_execution_breakpoint(0x2000, 1);
    assert(!event2_el1.has_value());

    // Breakpoint 2: match EL0 or EL1 (PMC=3).
    debug.set_dbgbvr(2, 0x3000);
    debug.set_dbgbcr(2, 0x1 | (3u << 1)); // E=1, PMC=3 (EL0_EL1)

    auto event3_el0 = debug.check_execution_breakpoint(0x3000, 0);
    assert(event3_el0.has_value());
    auto event3_el1 = debug.check_execution_breakpoint(0x3000, 1);
    assert(event3_el1.has_value());

    std::cout << "  PASS\n";
}

void test_multiple_breakpoints() {
    std::cout << "Test: Multiple breakpoints, priority by slot order\n";
    winios::DebugArchitecture debug;

    debug.set_mdscr_el1(1ULL << 15);

    // Configure all four breakpoints at different addresses.
    for (std::size_t i = 0; i < 4; ++i) {
        debug.set_dbgbvr(i, 0x1000 + i * 0x100);
        debug.set_dbgbcr(i, 0x1); // Enable all
    }

    // Match breakpoint 0.
    auto event0 = debug.check_execution_breakpoint(0x1000, 1);
    assert(event0.has_value());
    assert(event0->slot_index == 0);

    // Match breakpoint 2.
    auto event2 = debug.check_execution_breakpoint(0x1200, 1);
    assert(event2.has_value());
    assert(event2->slot_index == 2);

    // Match breakpoint 3.
    auto event3 = debug.check_execution_breakpoint(0x1300, 1);
    assert(event3.has_value());
    assert(event3->slot_index == 3);

    // Non-matching address.
    auto event_none = debug.check_execution_breakpoint(0x2000, 1);
    assert(!event_none.has_value());

    std::cout << "  PASS\n";
}

void test_watchpoint_matching_read() {
    std::cout << "Test: Watchpoint read matching\n";
    winios::DebugArchitecture debug;

    debug.set_mdscr_el1(1ULL << 15);

    // Configure watchpoint 0 for loads at VA 0x2000, BAS=0xFF (all bytes).
    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, 0x1 | (1u << 3) | (0xFFu << 5)); // E=1, LSC=1 (load), BAS=0xFF

    // Read from 0x2000 should match.
    auto event = debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1);
    assert(event.has_value());
    assert(event->type == winios::DebugEventType::Watchpoint);
    assert(event->address == 0x2000);
    assert(event->access == winios::DebugAccessType::Read);
    assert(event->access_size == 4);
    assert(event->slot_index == 0);

    // Write to 0x2000 should not match (LSC=1 is load-only).
    auto event2 = debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Write, 1);
    assert(!event2.has_value());

    std::cout << "  PASS\n";
}

void test_watchpoint_matching_write() {
    std::cout << "Test: Watchpoint write matching\n";
    winios::DebugArchitecture debug;

    debug.set_mdscr_el1(1ULL << 15);

    // Configure watchpoint 1 for stores at VA 0x3000, BAS=0xFF.
    debug.set_dbgwvr(1, 0x3000);
    debug.set_dbgwcr(1, 0x1 | (2u << 3) | (0xFFu << 5)); // E=1, LSC=2 (store), BAS=0xFF

    // Write to 0x3000 should match.
    auto event = debug.check_data_watchpoint(0x3000, 8, winios::DebugAccessType::Write, 1);
    assert(event.has_value());
    assert(event->type == winios::DebugEventType::Watchpoint);
    assert(event->access == winios::DebugAccessType::Write);
    assert(event->slot_index == 1);

    // Read from 0x3000 should not match (LSC=2 is store-only).
    auto event2 = debug.check_data_watchpoint(0x3000, 8, winios::DebugAccessType::Read, 1);
    assert(!event2.has_value());

    std::cout << "  PASS\n";
}

void test_watchpoint_matching_both() {
    std::cout << "Test: Watchpoint read/write matching\n";
    winios::DebugArchitecture debug;

    debug.set_mdscr_el1(1ULL << 15);

    // Configure watchpoint 2 for both loads and stores at VA 0x4000, BAS=0xFF.
    debug.set_dbgwvr(2, 0x4000);
    debug.set_dbgwcr(2, 0x1 | (3u << 3) | (0xFFu << 5)); // E=1, LSC=3 (both), BAS=0xFF

    // Read should match.
    auto event_read = debug.check_data_watchpoint(0x4000, 4, winios::DebugAccessType::Read, 1);
    assert(event_read.has_value());
    assert(event_read->slot_index == 2);

    // Write should match.
    auto event_write = debug.check_data_watchpoint(0x4000, 4, winios::DebugAccessType::Write, 1);
    assert(event_write.has_value());
    assert(event_write->slot_index == 2);

    std::cout << "  PASS\n";
}

void test_watchpoint_bas_byte_granular() {
    std::cout << "Test: Watchpoint BAS byte-granular matching\n";
    winios::DebugArchitecture debug;

    debug.set_mdscr_el1(1ULL << 15);

    // Watchpoint at 0x5000 (8-byte aligned), BAS=0b00001111 (bytes 0-3).
    debug.set_dbgwvr(0, 0x5000);
    debug.set_dbgwcr(0, 0x1 | (3u << 3) | (0x0Fu << 5)); // E=1, LSC=3, BAS=0x0F

    // Access to 0x5000 (byte 0) should match.
    auto event1 = debug.check_data_watchpoint(0x5000, 1, winios::DebugAccessType::Read, 1);
    assert(event1.has_value());

    // Access to 0x5003 (byte 3) should match.
    auto event2 = debug.check_data_watchpoint(0x5003, 1, winios::DebugAccessType::Write, 1);
    assert(event2.has_value());

    // Access to 0x5004 (byte 4) should NOT match (BAS[4]=0).
    auto event3 = debug.check_data_watchpoint(0x5004, 1, winios::DebugAccessType::Read, 1);
    assert(!event3.has_value());

    // Access to 0x5007 (byte 7) should NOT match (BAS[7]=0).
    auto event4 = debug.check_data_watchpoint(0x5007, 1, winios::DebugAccessType::Write, 1);
    assert(!event4.has_value());

    // Multi-byte access spanning bytes 2-5: should match (byte 2 and 3 are in BAS).
    auto event5 = debug.check_data_watchpoint(0x5002, 4, winios::DebugAccessType::Read, 1);
    assert(event5.has_value());

    std::cout << "  PASS\n";
}

void test_watchpoint_bas_upper_bytes() {
    std::cout << "Test: Watchpoint BAS upper bytes\n";
    winios::DebugArchitecture debug;

    debug.set_mdscr_el1(1ULL << 15);

    // Watchpoint at 0x6000, BAS=0b11110000 (bytes 4-7).
    debug.set_dbgwvr(1, 0x6000);
    debug.set_dbgwcr(1, 0x1 | (3u << 3) | (0xF0u << 5)); // E=1, LSC=3, BAS=0xF0

    // Access to 0x6004 (byte 4) should match.
    auto event1 = debug.check_data_watchpoint(0x6004, 4, winios::DebugAccessType::Write, 1);
    assert(event1.has_value());

    // Access to 0x6003 (byte 3) should NOT match.
    auto event2 = debug.check_data_watchpoint(0x6003, 1, winios::DebugAccessType::Read, 1);
    assert(!event2.has_value());

    std::cout << "  PASS\n";
}

void test_watchpoint_privilege_filtering() {
    std::cout << "Test: Watchpoint privilege filtering\n";
    winios::DebugArchitecture debug;

    debug.set_mdscr_el1(1ULL << 15);

    // Watchpoint 0: match only EL1 (PAC=1).
    debug.set_dbgwvr(0, 0x7000);
    debug.set_dbgwcr(0, 0x1 | (1u << 1) | (3u << 3) | (0xFFu << 5)); // E=1, PAC=1, LSC=3, BAS=0xFF

    // Should match at EL1.
    auto event_el1 = debug.check_data_watchpoint(0x7000, 4, winios::DebugAccessType::Read, 1);
    assert(event_el1.has_value());

    // Should not match at EL0.
    auto event_el0 = debug.check_data_watchpoint(0x7000, 4, winios::DebugAccessType::Read, 0);
    assert(!event_el0.has_value());

    std::cout << "  PASS\n";
}

void test_monitor_debug_disabled() {
    std::cout << "Test: Monitor debug disabled prevents matching\n";
    winios::DebugArchitecture debug;

    // Configure breakpoint and watchpoint, but leave MDE disabled.
    debug.set_dbgbvr(0, 0x1000);
    debug.set_dbgbcr(0, 0x1); // Enabled
    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, 0x1 | (3u << 3) | (0xFFu << 5)); // Enabled

    // MDE disabled: no matches.
    assert(!debug.monitor_debug_enabled());
    auto bp_event = debug.check_execution_breakpoint(0x1000, 1);
    assert(!bp_event.has_value());
    auto wp_event = debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Write, 1);
    assert(!wp_event.has_value());

    // Enable MDE.
    debug.set_mdscr_el1(1ULL << 15);
    assert(debug.monitor_debug_enabled());

    // Now matches should occur.
    bp_event = debug.check_execution_breakpoint(0x1000, 1);
    assert(bp_event.has_value());
    wp_event = debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Write, 1);
    assert(wp_event.has_value());

    std::cout << "  PASS\n";
}

void test_concurrent_access() {
    std::cout << "Test: Concurrent state access (thread-safety)\n";

    // Create four independent debug architectures (simulating four cores).
    std::vector<winios::DebugArchitecture> cores(4);

    // Launch four threads, each configuring its own core's debug state.
    std::vector<std::thread> threads;
    for (std::size_t core_id = 0; core_id < 4; ++core_id) {
        threads.emplace_back([&cores, core_id]() {
            auto& debug = cores[core_id];
            // Each thread writes to its own core's debug registers.
            for (std::size_t i = 0; i < 100; ++i) {
                debug.set_mdscr_el1(1ULL << 15);
                debug.set_dbgbvr(0, 0x1000 * (core_id + 1) + i);
                debug.set_dbgbcr(0, 0x1);
                debug.set_dbgwvr(1, 0x2000 * (core_id + 1) + i);
                debug.set_dbgwcr(1, 0xFF01);

                // Read back and verify.
                assert(debug.monitor_debug_enabled());
                assert(debug.get_mdscr_el1() == (1ULL << 15));
            }
        });
    }

    // Wait for all threads to complete.
    for (auto& t : threads) {
        t.join();
    }

    // Verify final state is consistent (no data races).
    for (std::size_t core_id = 0; core_id < 4; ++core_id) {
        assert(cores[core_id].monitor_debug_enabled());
        assert(cores[core_id].get_mdscr_el1() == (1ULL << 15));
        assert(cores[core_id].get_dbgbvr(0) == 0x1000 * (core_id + 1) + 99);
        assert(cores[core_id].get_dbgwvr(1) == 0x2000 * (core_id + 1) + 99);
    }

    std::cout << "  PASS\n";
}

void test_inspect_output() {
    std::cout << "Test: Inspect output\n";
    winios::DebugArchitecture debug;

    debug.set_mdscr_el1(1ULL << 15);
    debug.set_dbgbvr(0, 0x1234);
    debug.set_dbgbcr(0, 0x1);
    debug.set_dbgwvr(1, 0x5678);
    debug.set_dbgwcr(1, 0xFF09); // E=1, LSC=1, BAS=0xFF

    std::string output = debug.inspect();
    
    // Verify output contains key information.
    assert(output.find("MDSCR_EL1") != std::string::npos);
    assert(output.find("enabled") != std::string::npos);
    assert(output.find("DBGBVR") != std::string::npos);
    assert(output.find("DBGWCR") != std::string::npos);

    std::cout << "  Inspect output:\n" << output;
    std::cout << "  PASS\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Phase 51 Stage 2: Debug Architecture Foundation Tests ===\n\n";

    try {
        test_default_state();
        test_reset_behavior();
        test_four_slot_limits();
        test_independent_state_across_cores();
        test_breakpoint_configuration();
        test_watchpoint_configuration();
        test_mdscr_enable_disable();
        test_breakpoint_matching_exact();
        test_breakpoint_disabled();
        test_breakpoint_privilege_filtering();
        test_multiple_breakpoints();
        test_watchpoint_matching_read();
        test_watchpoint_matching_write();
        test_watchpoint_matching_both();
        test_watchpoint_bas_byte_granular();
        test_watchpoint_bas_upper_bytes();
        test_watchpoint_privilege_filtering();
        test_monitor_debug_disabled();
        test_concurrent_access();
        test_inspect_output();

        std::cout << "\n=== All tests PASSED ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n=== Test FAILED with exception: " << e.what() << " ===\n";
        return 1;
    }
}
