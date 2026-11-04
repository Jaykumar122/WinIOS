#include "cpu/cpu.h"
#include "cpu/debug_architecture.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

// WinIOS Phase 51 Stage 7: Linked Hardware Breakpoints and Watchpoints Test Suite
// Verifies all 40 test categories:
// - Linked execution breakpoints (1-12)
// - Linked data watchpoints (13-30)
// - Integration, system registers, reset, SMP, and parallel execution (31-40)

namespace {

// Helper: encode DBGBCR<n>_EL1
// E[0], PMC[2:1], BAS[8:5], LBN[19:16], BT[23:20]
constexpr std::uint32_t make_bcr(bool enable, unsigned pmc, unsigned bas, unsigned lbn, unsigned bt) {
    std::uint32_t val = 0;
    if (enable) val |= 0x1u;
    val |= (pmc & 0x3u) << 1u;
    val |= (bas & 0xFu) << 5u;
    val |= (lbn & 0xFu) << 16u;
    val |= (bt & 0xFu) << 20u;
    return val;
}

// Helper: encode DBGWCR<n>_EL1
// E[0], PAC[2:1], LSC[4:3], BAS[12:5], LBN[19:16], WT[20]
constexpr std::uint32_t make_wcr(bool enable, unsigned pac, unsigned lsc, unsigned bas, unsigned lbn, unsigned wt) {
    std::uint32_t val = 0;
    if (enable) val |= 0x1u;
    val |= (pac & 0x3u) << 1u;
    val |= (lsc & 0x3u) << 3u;
    val |= (bas & 0xFFu) << 5u;
    val |= (lbn & 0xFu) << 16u;
    if (wt) val |= (1u << 20u);
    return val;
}

// Helper: setup CPU with memory and vector tables
void setup_cpu(CPU& cpu, Memory& mem, unsigned el = 1, std::uint64_t vbar = 0x10000) {
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(el);
    cpu.set_vbar_el1(vbar);
    cpu.set_vbar_el2(vbar + 0x10000);
    cpu.set_vbar_el3(vbar + 0x20000);
    for (std::size_t i = 0; i < mem.size(); i += 4) {
        mem.write_u32(i, 0xD503201Fu); // NOP
    }
}

// =========================================================================
// Category 1: Linked Execution Breakpoints (Tests 1-12)
// =========================================================================

void test_1_bp0_linked_to_bp1_matching() {
    std::cout << "Test 1: Breakpoint 0 linked to Breakpoint 1 matching\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000); // MDE = 1

    // BP0: linked address match (BT=1), LBN=1, address=0x1000
    debug.set_dbgbvr(0, 0x1000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 1, 1));

    // BP1: unlinked address match (BT=0), address=0x1000
    debug.set_dbgbvr(1, 0x1000);
    debug.set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 0, 0));

    // Evaluate at PC = 0x1000 -> both match
    auto ev = debug.check_execution_breakpoint(0x1000, 1);
    assert(ev.has_value());
    assert(ev->type == winios::DebugEventType::Breakpoint);
    assert(ev->slot_index == 0);
    assert(ev->address == 0x1000);
    std::cout << "  PASS\n";
}

void test_2_bp0_linked_to_bp1_not_matching() {
    std::cout << "Test 2: Breakpoint 0 linked to Breakpoint 1 not matching\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // BP0: linked address match, LBN=1, address=0x1000
    debug.set_dbgbvr(0, 0x1000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 1, 1));

    // BP1: unlinked address match, address=0x2000 (different address!)
    debug.set_dbgbvr(1, 0x2000);
    debug.set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 0, 0));

    // Evaluate at PC = 0x1000 -> BP1 condition fails
    auto ev = debug.check_execution_breakpoint(0x1000, 1);
    assert(!ev.has_value());
    std::cout << "  PASS\n";
}

void test_3_bp1_linked_to_bp2() {
    std::cout << "Test 3: Breakpoint 1 linked to Breakpoint 2\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // BP1: linked to BP2 (LBN=2), address=0x3000
    debug.set_dbgbvr(1, 0x3000);
    debug.set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 2, 1));

    // BP2: unlinked match at 0x3000
    debug.set_dbgbvr(2, 0x3000);
    debug.set_dbgbcr(2, make_bcr(true, 0b11, 0b1111, 0, 0));

    auto ev = debug.check_execution_breakpoint(0x3000, 1);
    assert(ev.has_value());
    assert(ev->slot_index == 1);

    // Change BP2 address -> should not trigger
    debug.set_dbgbvr(2, 0x3004);
    assert(!debug.check_execution_breakpoint(0x3000, 1).has_value());
    std::cout << "  PASS\n";
}

void test_4_bp2_linked_to_bp3() {
    std::cout << "Test 4: Breakpoint 2 linked to Breakpoint 3\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // BP2 linked to BP3 (LBN=3) at 0x4000
    debug.set_dbgbvr(2, 0x4000);
    debug.set_dbgbcr(2, make_bcr(true, 0b11, 0b1111, 3, 1));

    // BP3 unlinked match at 0x4000
    debug.set_dbgbvr(3, 0x4000);
    debug.set_dbgbcr(3, make_bcr(true, 0b11, 0b1111, 0, 0));

    auto ev = debug.check_execution_breakpoint(0x4000, 1);
    assert(ev.has_value());
    assert(ev->slot_index == 2);
    std::cout << "  PASS\n";
}

void test_5_bp_linked_to_self() {
    std::cout << "Test 5: Breakpoint linked to self (prohibited configuration)\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // BP0 linked to BP0 (LBN=0)
    debug.set_dbgbvr(0, 0x1000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, 1));

    auto ev = debug.check_execution_breakpoint(0x1000, 1);
    assert(!ev.has_value());
    std::cout << "  PASS\n";
}

void test_6_bp_linked_to_unimplemented_slot() {
    std::cout << "Test 6: Breakpoint linked to unimplemented slot\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // BP0 linked to LBN=4 (only slots 0..3 exist)
    debug.set_dbgbvr(0, 0x1000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 4, 1));

    assert(!debug.check_execution_breakpoint(0x1000, 1).has_value());

    // BP0 linked to LBN=15
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 15, 1));
    assert(!debug.check_execution_breakpoint(0x1000, 1).has_value());
    std::cout << "  PASS\n";
}

void test_7_bp_linked_to_disabled_slot() {
    std::cout << "Test 7: Breakpoint linked to disabled slot\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // BP0 enabled, linked to BP1
    debug.set_dbgbvr(0, 0x1000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 1, 1));

    // BP1 disabled (E=0)
    debug.set_dbgbvr(1, 0x1000);
    debug.set_dbgbcr(1, make_bcr(false, 0b11, 0b1111, 0, 0));

    assert(!debug.check_execution_breakpoint(0x1000, 1).has_value());
    std::cout << "  PASS\n";
}

void test_8_bp_with_invalid_bt_encoding() {
    std::cout << "Test 8: Breakpoint with invalid BT encoding\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // Reserved BT values: 2, 4, 5, 8, 15
    for (unsigned bt : {2u, 4u, 5u, 8u, 15u}) {
        debug.set_dbgbvr(0, 0x1000);
        debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, bt));
        assert(!debug.check_execution_breakpoint(0x1000, 1).has_value());
    }
    std::cout << "  PASS\n";
}

void test_9_multiple_linked_breakpoints_in_cascade() {
    std::cout << "Test 9: Multiple linked breakpoints in cascade\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // Cascade: BP0 -> BP1 -> BP2 -> base BP2 (unlinked match at 0x5000)
    debug.set_dbgbvr(0, 0x5000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 1, 1)); // BP0 links to BP1

    debug.set_dbgbvr(1, 0x5000);
    debug.set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 2, 1)); // BP1 links to BP2

    debug.set_dbgbvr(2, 0x5000);
    debug.set_dbgbcr(2, make_bcr(true, 0b11, 0b1111, 0, 0)); // BP2 unlinked base

    // All match -> fires BP0 (highest priority slot)
    auto ev = debug.check_execution_breakpoint(0x5000, 1);
    assert(ev.has_value());
    assert(ev->slot_index == 0);

    // Disable BP2 -> cascade fails completely
    debug.set_dbgbcr(2, make_bcr(false, 0b11, 0b1111, 0, 0));
    assert(!debug.check_execution_breakpoint(0x5000, 1).has_value());
    std::cout << "  PASS\n";
}

void test_10_cascade_cycle_detection() {
    std::cout << "Test 10: Cascade cycle detection (BP0 -> BP1 -> BP0)\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(0, 0x6000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 1, 1)); // BP0 -> BP1

    debug.set_dbgbvr(1, 0x6000);
    debug.set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 0, 1)); // BP1 -> BP0 (cycle!)

    // Cycle detected, must terminate safely without triggering
    assert(!debug.check_execution_breakpoint(0x6000, 1).has_value());
    std::cout << "  PASS\n";
}

void test_11_unlinked_breakpoints_unaffected() {
    std::cout << "Test 11: Unlinked breakpoints unaffected by linked configurations\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // BP0 is standard unlinked breakpoint at 0x7000
    debug.set_dbgbvr(0, 0x7000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, 0));

    // BP1 and BP2 have linked configurations
    debug.set_dbgbvr(1, 0x8000);
    debug.set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 2, 1));
    debug.set_dbgbvr(2, 0x9000);
    debug.set_dbgbcr(2, make_bcr(false, 0b11, 0b1111, 0, 0));

    // BP0 fires normally at 0x7000
    auto ev = debug.check_execution_breakpoint(0x7000, 1);
    assert(ev.has_value());
    assert(ev->slot_index == 0);
    std::cout << "  PASS\n";
}

void test_12_privilege_filtering_in_linked_mode() {
    std::cout << "Test 12: Breakpoint privilege level filtering preserved in linked mode\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // BP0 allows EL1 (PMC=0b01)
    debug.set_dbgbvr(0, 0x1000);
    debug.set_dbgbcr(0, make_bcr(true, 0b01, 0b1111, 1, 1));

    // BP1 allows only EL0 (PMC=0b10)
    debug.set_dbgbvr(1, 0x1000);
    debug.set_dbgbcr(1, make_bcr(true, 0b10, 0b1111, 0, 0));

    // At EL1: BP0 PMC matches, but BP1 PMC rejects EL1 -> no match!
    assert(!debug.check_execution_breakpoint(0x1000, 1).has_value());

    // Update BP1 to allow EL1 (PMC=0b01) -> now matches!
    debug.set_dbgbcr(1, make_bcr(true, 0b01, 0b1111, 0, 0));
    auto ev = debug.check_execution_breakpoint(0x1000, 1);
    assert(ev.has_value());
    assert(ev->slot_index == 0);
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 2: Linked Data Watchpoints (Tests 13-30)
// =========================================================================

void test_13_wp0_linked_to_bp0_matching() {
    std::cout << "Test 13: Watchpoint 0 linked to Breakpoint 0 matching\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // BP0 at instruction 0x8000
    debug.set_dbgbvr(0, 0x8000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, 0));

    // WP0 linked to BP0 (LBN=0, WT=1) for data address 0x2000
    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b11, 0xFF, 0, 1));

    // Memory access at 0x2000 executed by instruction at 0x8000 -> MATCH!
    auto ev = debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1, 0x8000);
    assert(ev.has_value());
    assert(ev->type == winios::DebugEventType::Watchpoint);
    assert(ev->slot_index == 0);
    assert(ev->address == 0x2000);
    std::cout << "  PASS\n";
}

void test_14_wp0_linked_to_bp0_not_matching() {
    std::cout << "Test 14: Watchpoint 0 linked to Breakpoint 0 not matching instruction\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(0, 0x8000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, 0));

    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b11, 0xFF, 0, 1));

    // Memory access at 0x2000 executed by instruction at 0x9000 (different PC!) -> NO MATCH
    auto ev = debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1, 0x9000);
    assert(!ev.has_value());
    std::cout << "  PASS\n";
}

void test_15_wp_linked_to_bp1() {
    std::cout << "Test 15: Watchpoint linked to Breakpoint 1\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(1, 0xA000);
    debug.set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 0, 0));

    debug.set_dbgwvr(0, 0x3000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b11, 0xFF, 1, 1));

    assert(debug.check_data_watchpoint(0x3000, 4, winios::DebugAccessType::Write, 1, 0xA000).has_value());
    assert(!debug.check_data_watchpoint(0x3000, 4, winios::DebugAccessType::Write, 1, 0xB000).has_value());
    std::cout << "  PASS\n";
}

void test_16_wp_linked_to_bp2() {
    std::cout << "Test 16: Watchpoint linked to Breakpoint 2\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(2, 0xB000);
    debug.set_dbgbcr(2, make_bcr(true, 0b11, 0b1111, 0, 0));

    debug.set_dbgwvr(1, 0x4000);
    debug.set_dbgwcr(1, make_wcr(true, 0b11, 0b11, 0xFF, 2, 1));

    assert(debug.check_data_watchpoint(0x4000, 8, winios::DebugAccessType::Read, 1, 0xB000).has_value());
    std::cout << "  PASS\n";
}

void test_17_wp_linked_to_bp3() {
    std::cout << "Test 17: Watchpoint linked to Breakpoint 3\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(3, 0xC000);
    debug.set_dbgbcr(3, make_bcr(true, 0b11, 0b1111, 0, 0));

    debug.set_dbgwvr(2, 0x5000);
    debug.set_dbgwcr(2, make_wcr(true, 0b11, 0b11, 0xFF, 3, 1));

    assert(debug.check_data_watchpoint(0x5000, 4, winios::DebugAccessType::Write, 1, 0xC000).has_value());
    std::cout << "  PASS\n";
}

void test_18_wp_linked_to_unimplemented_bp() {
    std::cout << "Test 18: Watchpoint linked to unimplemented Breakpoint slot\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b11, 0xFF, 4, 1)); // LBN=4 (invalid)

    assert(!debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1, 0x1000).has_value());
    std::cout << "  PASS\n";
}

void test_19_wp_linked_to_disabled_bp() {
    std::cout << "Test 19: Watchpoint linked to disabled Breakpoint\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // BP0 disabled
    debug.set_dbgbvr(0, 0x8000);
    debug.set_dbgbcr(0, make_bcr(false, 0b11, 0b1111, 0, 0));

    // WP0 enabled, WT=1, LBN=0
    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b11, 0xFF, 0, 1));

    // Fails because linked BP is disabled
    assert(!debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1, 0x8000).has_value());
    std::cout << "  PASS\n";
}

void test_20_wp_with_wt0_behaving_unlinked() {
    std::cout << "Test 20: Watchpoint with WT=0 behaving as unlinked watchpoint\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // WP0 with WT=0 (unlinked), LBN set to arbitrary number (e.g. 5)
    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b11, 0xFF, 5, 0));

    // Fires regardless of instruction PC or BP status
    auto ev = debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1, 0x0);
    assert(ev.has_value());
    assert(ev->slot_index == 0);
    std::cout << "  PASS\n";
}

void test_21_wp_with_wt1_requiring_bp_match() {
    std::cout << "Test 21: Watchpoint with WT=1 requiring Breakpoint match\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(0, 0x8000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, 0));

    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b11, 0xFF, 0, 1));

    // Matches with PC 0x8000
    assert(debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1, 0x8000).has_value());
    // Fails with PC 0x8004
    assert(!debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1, 0x8004).has_value());
    std::cout << "  PASS\n";
}

void test_22_wp_bas_preserved_in_linked_mode() {
    std::cout << "Test 22: Watchpoint Byte Address Select (BAS) preserved in linked mode\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(0, 0x8000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, 0));

    // BAS = 0x0F (bytes 0..3 of 8-byte aligned region 0x2000)
    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b11, 0x0F, 0, 1));

    // Access to byte 2 (0x2002) matches
    assert(debug.check_data_watchpoint(0x2002, 1, winios::DebugAccessType::Read, 1, 0x8000).has_value());
    // Access to byte 5 (0x2005) does not match
    assert(!debug.check_data_watchpoint(0x2005, 1, winios::DebugAccessType::Read, 1, 0x8000).has_value());
    std::cout << "  PASS\n";
}

void test_23_wp_lsc_preserved_in_linked_mode() {
    std::cout << "Test 23: Watchpoint Load/Store Control (LSC) preserved in linked mode\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(0, 0x8000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, 0));

    // LSC = 0b01 (Load only)
    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b01, 0xFF, 0, 1));

    assert(debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1, 0x8000).has_value());
    assert(!debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Write, 1, 0x8000).has_value());
    std::cout << "  PASS\n";
}

void test_24_wp_pac_filtering_in_linked_mode() {
    std::cout << "Test 24: Watchpoint privilege level filtering preserved in linked mode\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(0, 0x8000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, 0));

    // PAC = 0b01 (EL1 only)
    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, make_wcr(true, 0b01, 0b11, 0xFF, 0, 1));

    // At EL1 -> matches
    assert(debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1, 0x8000).has_value());
    // At EL0 -> fails
    assert(!debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 0, 0x8000).has_value());
    std::cout << "  PASS\n";
}

void test_25_linked_bp_pac_filtering() {
    std::cout << "Test 25: Linked Breakpoint privilege level filtering applied to watchpoint\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    // BP0 configured for EL0 only (PMC = 0b10)
    debug.set_dbgbvr(0, 0x8000);
    debug.set_dbgbcr(0, make_bcr(true, 0b10, 0b1111, 0, 0));

    // WP0 configured for EL0 and EL1 (PAC = 0b11)
    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b11, 0xFF, 0, 1));

    // At EL1: WP PAC allows it, but BP PMC rejects EL1 -> no match!
    assert(!debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1, 0x8000).has_value());
    // At EL0: Both allow EL0 -> matches!
    assert(debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 0, 0x8000).has_value());
    std::cout << "  PASS\n";
}

void test_26_read_watchpoint_linked_to_breakpoint() {
    std::cout << "Test 26: Read watchpoint linked to breakpoint\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(1, 0x1400);
    debug.set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 0, 0));

    debug.set_dbgwvr(0, 0x5000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b01, 0xFF, 1, 1)); // Read only

    auto ev = debug.check_data_watchpoint(0x5000, 4, winios::DebugAccessType::Read, 1, 0x1400);
    assert(ev.has_value());
    assert(ev->access == winios::DebugAccessType::Read);
    std::cout << "  PASS\n";
}

void test_27_write_watchpoint_linked_to_breakpoint() {
    std::cout << "Test 27: Write watchpoint linked to breakpoint\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(2, 0x1800);
    debug.set_dbgbcr(2, make_bcr(true, 0b11, 0b1111, 0, 0));

    debug.set_dbgwvr(0, 0x5000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b10, 0xFF, 2, 1)); // Write only

    auto ev = debug.check_data_watchpoint(0x5000, 4, winios::DebugAccessType::Write, 1, 0x1800);
    assert(ev.has_value());
    assert(ev->access == winios::DebugAccessType::Write);
    std::cout << "  PASS\n";
}

void test_28_read_write_watchpoint_linked_to_breakpoint() {
    std::cout << "Test 28: Read/Write watchpoint linked to breakpoint\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(0, 0x1C00);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, 0));

    debug.set_dbgwvr(1, 0x6000);
    debug.set_dbgwcr(1, make_wcr(true, 0b11, 0b11, 0xFF, 0, 1)); // Read/Write

    assert(debug.check_data_watchpoint(0x6000, 4, winios::DebugAccessType::Read, 1, 0x1C00).has_value());
    assert(debug.check_data_watchpoint(0x6000, 4, winios::DebugAccessType::Write, 1, 0x1C00).has_value());
    std::cout << "  PASS\n";
}

void test_29_multiple_watchpoints_linked_to_different_breakpoints() {
    std::cout << "Test 29: Multiple watchpoints linked to different breakpoints\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(0, 0x2000); // BP0
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, 0));
    debug.set_dbgbvr(1, 0x2400); // BP1
    debug.set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 0, 0));

    debug.set_dbgwvr(0, 0x7000); // WP0 -> BP0
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b11, 0xFF, 0, 1));

    debug.set_dbgwvr(1, 0x8000); // WP1 -> BP1
    debug.set_dbgwcr(1, make_wcr(true, 0b11, 0b11, 0xFF, 1, 1));

    auto ev0 = debug.check_data_watchpoint(0x7000, 4, winios::DebugAccessType::Read, 1, 0x2000);
    assert(ev0.has_value() && ev0->slot_index == 0);

    auto ev1 = debug.check_data_watchpoint(0x8000, 4, winios::DebugAccessType::Read, 1, 0x2400);
    assert(ev1.has_value() && ev1->slot_index == 1);
    std::cout << "  PASS\n";
}

void test_30_multiple_watchpoints_linked_to_same_breakpoint() {
    std::cout << "Test 30: Multiple watchpoints linked to the same breakpoint\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);

    debug.set_dbgbvr(0, 0x3000); // Common BP0
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, 0));

    debug.set_dbgwvr(0, 0x7000); // WP0 -> BP0
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b11, 0xFF, 0, 1));

    debug.set_dbgwvr(1, 0x8000); // WP1 -> BP0 (different aligned address)
    debug.set_dbgwcr(1, make_wcr(true, 0b11, 0b11, 0xFF, 0, 1));

    auto ev0 = debug.check_data_watchpoint(0x7000, 4, winios::DebugAccessType::Read, 1, 0x3000);
    assert(ev0.has_value() && ev0->slot_index == 0);

    auto ev1 = debug.check_data_watchpoint(0x8000, 4, winios::DebugAccessType::Read, 1, 0x3000);
    assert(ev1.has_value() && ev1->slot_index == 1);
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 3: Integration, SysRegs, Reset, SMP, Parallel (Tests 31-40)
// =========================================================================

void test_31_sysreg_dbgbcr_lbn_bt_fields() {
    std::cout << "Test 31: System register access to DBGBCR with LBN and BT fields\n";
    CPU cpu;
    std::uint32_t val = make_bcr(true, 0b11, 0b1111, 2, 1);
    cpu.get_debug_arch().set_dbgbcr(0, val);

    assert(cpu.get_debug_arch().get_dbgbcr(0) == val);
    const auto& bp = cpu.get_debug_arch();
    assert(bp.get_dbgbcr(0) & 0x1); // E=1
    assert(((bp.get_dbgbcr(0) >> 16) & 0xF) == 2); // LBN=2
    assert(((bp.get_dbgbcr(0) >> 20) & 0xF) == 1); // BT=1
    std::cout << "  PASS\n";
}

void test_32_sysreg_dbgwcr_lbn_wt_fields() {
    std::cout << "Test 32: System register access to DBGWCR with LBN and WT fields\n";
    CPU cpu;
    std::uint32_t val = make_wcr(true, 0b11, 0b11, 0xFF, 3, 1);
    cpu.get_debug_arch().set_dbgwcr(1, val);

    assert(cpu.get_debug_arch().get_dbgwcr(1) == val);
    assert(((cpu.get_debug_arch().get_dbgwcr(1) >> 16) & 0xF) == 3); // LBN=3
    assert(((cpu.get_debug_arch().get_dbgwcr(1) >> 20) & 0x1) == 1); // WT=1
    std::cout << "  PASS\n";
}

void test_33_reset_clears_linked_debug_state() {
    std::cout << "Test 33: Reset clears linked debug state\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);
    debug.set_dbgbvr(0, 0x1000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 1, 1));
    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b11, 0xFF, 0, 1));

    debug.reset();

    assert(!debug.monitor_debug_enabled());
    assert(debug.get_dbgbcr(0) == 0);
    assert(debug.get_dbgwcr(0) == 0);
    assert(!debug.check_execution_breakpoint(0x1000, 1).has_value());
    assert(!debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1, 0x1000).has_value());
    std::cout << "  PASS\n";
}

void test_34_cpu_step_linked_breakpoint_exception() {
    std::cout << "Test 34: CPU step with linked breakpoint generates architectural exception\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, 0xD503201Fu); // NOP at 0x2000

    // Setup linked breakpoint: BP0 -> BP1 at 0x2000
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 1, 1));
    cpu.get_debug_arch().set_dbgbvr(1, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 0, 0));

    // Step CPU: must take breakpoint exception before executing instruction
    cpu.step(mem);

    // Vector entry taken: VBAR_EL1 + 0x200
    assert(cpu.get_program_counter() == 0x10200ULL);
    assert(cpu.get_elr_el1() == 0x2000ULL);
    const std::uint64_t esr = cpu.get_esr_el1();
    const unsigned ec = static_cast<unsigned>((esr >> 26u) & 0x3Fu);
    assert(ec == 0x30u || ec == 0x31u);
    std::cout << "  PASS\n";
}

void test_35_cpu_memory_access_linked_watchpoint_triggers() {
    std::cout << "Test 35: CPU memory access with matching linked watchpoint triggers exception\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    // STR W0, [X1] -> 0xB9000020u
    mem.write_u32(0x2000, 0xB9000020u);
    cpu.write_register(0, 0x42);
    cpu.write_register(1, 0x5000); // Data address 0x5000

    // Setup linked watchpoint: WP0 -> BP0 at instruction 0x2000
    // BP0 configured with BT=3 (linked target) so it does not fire as an execution breakpoint
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000); // Instruction PC = 0x2000
    cpu.get_debug_arch().set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, 3));

    cpu.get_debug_arch().set_dbgwvr(0, 0x5000); // Data address = 0x5000
    cpu.get_debug_arch().set_dbgwcr(0, make_wcr(true, 0b11, 0b10, 0xFF, 0, 1)); // Write only, linked to BP0

    cpu.step(mem);

    assert(cpu.get_program_counter() == 0x10200ULL);
    const std::uint64_t esr = cpu.get_esr_el1();
    const unsigned ec = static_cast<unsigned>((esr >> 26u) & 0x3Fu);
    assert(ec == 0x34u || ec == 0x35u); // Watchpoint EC
    assert(cpu.get_far_el1() == 0x5000ULL); // FAR holds data address
    assert(cpu.get_elr_el1() == 0x2000ULL); // ELR holds instruction address
    std::cout << "  PASS\n";
}

void test_36_cpu_memory_access_linked_watchpoint_instruction_mismatch() {
    std::cout << "Test 36: CPU memory access with mismatched linked watchpoint instruction does NOT trigger\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    // STR W0, [X1] -> 0xB9000020u
    mem.write_u32(0x2000, 0xB9000020u);
    cpu.write_register(0, 0x42);
    cpu.write_register(1, 0x5000);

    // Setup linked watchpoint: WP0 -> BP0 at instruction 0x3000 (mismatched!)
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x3000); // Different PC!
    cpu.get_debug_arch().set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 0, 0));

    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_wcr(true, 0b11, 0b10, 0xFF, 0, 1));

    cpu.step(mem);

    // PC advanced normally to 0x2004 without exception
    assert(cpu.get_program_counter() == 0x2004ULL);
    // Memory write succeeded
    assert(mem.read_u32(0x5000) == 0x42);
    std::cout << "  PASS\n";
}

void test_37_syndrome_and_far_correctness() {
    std::cout << "Test 37: Centralized exception syndrome and FAR correctness for linked watchpoint\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    // LDR W0, [X1] -> 0xB9400020u
    mem.write_u32(0x2000, 0xB9400020u);
    cpu.write_register(1, 0x6500);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(1, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 0, 3)); // BT=3 (link target)

    cpu.get_debug_arch().set_dbgwvr(2, 0x6500);
    cpu.get_debug_arch().set_dbgwcr(2, make_wcr(true, 0b11, 0b01, 0xFF, 1, 1)); // Read only

    cpu.step(mem);

    const std::uint64_t esr = cpu.get_esr_el1();
    assert(cpu.get_far_el1() == 0x6500ULL);
    // WnR bit (bit 6) must be 0 for read
    assert(((esr >> 6u) & 0x1u) == 0);
    std::cout << "  PASS\n";
}

void test_38_smp_cluster_multicore_debug_isolation() {
    std::cout << "Test 38: SMP cluster multicore debug state isolation\n";
    winios::SMPCluster cluster(2, 0x20000);
    auto& core0 = cluster.get_core(0);
    auto& core1 = cluster.get_core(1);

    // Configure linked breakpoint on Core 0
    core0.get_debug_arch().set_mdscr_el1(0x8000);
    core0.get_debug_arch().set_dbgbvr(0, 0x1000);
    core0.get_debug_arch().set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 1, 1));
    core0.get_debug_arch().set_dbgbvr(1, 0x1000);
    core0.get_debug_arch().set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 0, 0));

    // Core 1 debug registers must remain reset/disabled
    assert(!core1.get_debug_arch().monitor_debug_enabled());
    assert(core1.get_debug_arch().get_dbgbcr(0) == 0);
    assert(core1.get_debug_arch().get_dbgbcr(1) == 0);

    // Core 0 matches
    assert(core0.get_debug_arch().check_execution_breakpoint(0x1000, 1).has_value());
    // Core 1 does not match
    assert(!core1.get_debug_arch().check_execution_breakpoint(0x1000, 1).has_value());
    std::cout << "  PASS\n";
}

void test_39_multithreaded_parallel_matching_safety() {
    std::cout << "Test 39: Multithreaded parallel matching safety\n";
    winios::DebugArchitecture debug;
    debug.set_mdscr_el1(0x8000);
    debug.set_dbgbvr(0, 0x1000);
    debug.set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 1, 1));
    debug.set_dbgbvr(1, 0x1000);
    debug.set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 0, 0));

    debug.set_dbgwvr(0, 0x2000);
    debug.set_dbgwcr(0, make_wcr(true, 0b11, 0b11, 0xFF, 0, 1));

    std::vector<std::thread> workers;
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&debug, t]() {
            for (int i = 0; i < 500; ++i) {
                if (t % 2 == 0) {
                    auto ev = debug.check_execution_breakpoint(0x1000, 1);
                    assert(ev.has_value());
                } else {
                    auto ev = debug.check_data_watchpoint(0x2000, 4, winios::DebugAccessType::Read, 1, 0x1000);
                    assert(ev.has_value());
                }
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }
    std::cout << "  PASS\n";
}

void test_40_end_to_end_exception_return_and_recovery() {
    std::cout << "Test 40: End-to-end exception return and instruction recovery\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, 0xD503201Fu); // NOP
    mem.write_u32(0x2004, 0xD503201Fu); // NOP

    // Configure linked breakpoint at 0x2000 (BP0 -> BP1, BP1 is BT=3 link target)
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_bcr(true, 0b11, 0b1111, 1, 1));
    cpu.get_debug_arch().set_dbgbvr(1, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(1, make_bcr(true, 0b11, 0b1111, 0, 3));

    // Step 1: Takes breakpoint exception
    cpu.step(mem);
    assert(cpu.get_elr_el1() == 0x2000ULL);

    // Disable breakpoint in handler
    cpu.get_debug_arch().set_dbgbcr(0, 0);

    // ERET recovery: return to ELR_EL1
    cpu.set_program_counter(cpu.get_elr_el1());

    // Step 2: Now executes the instruction at 0x2000 and advances to 0x2004
    cpu.step(mem);
    assert(cpu.get_program_counter() == 0x2004ULL);
    std::cout << "  PASS\n";
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 51 Stage 7: Linked Debug Tests ===\n";

    // Category 1: Linked Execution Breakpoints (1-12)
    test_1_bp0_linked_to_bp1_matching();
    test_2_bp0_linked_to_bp1_not_matching();
    test_3_bp1_linked_to_bp2();
    test_4_bp2_linked_to_bp3();
    test_5_bp_linked_to_self();
    test_6_bp_linked_to_unimplemented_slot();
    test_7_bp_linked_to_disabled_slot();
    test_8_bp_with_invalid_bt_encoding();
    test_9_multiple_linked_breakpoints_in_cascade();
    test_10_cascade_cycle_detection();
    test_11_unlinked_breakpoints_unaffected();
    test_12_privilege_filtering_in_linked_mode();

    // Category 2: Linked Data Watchpoints (13-30)
    test_13_wp0_linked_to_bp0_matching();
    test_14_wp0_linked_to_bp0_not_matching();
    test_15_wp_linked_to_bp1();
    test_16_wp_linked_to_bp2();
    test_17_wp_linked_to_bp3();
    test_18_wp_linked_to_unimplemented_bp();
    test_19_wp_linked_to_disabled_bp();
    test_20_wp_with_wt0_behaving_unlinked();
    test_21_wp_with_wt1_requiring_bp_match();
    test_22_wp_bas_preserved_in_linked_mode();
    test_23_wp_lsc_preserved_in_linked_mode();
    test_24_wp_pac_filtering_in_linked_mode();
    test_25_linked_bp_pac_filtering();
    test_26_read_watchpoint_linked_to_breakpoint();
    test_27_write_watchpoint_linked_to_breakpoint();
    test_28_read_write_watchpoint_linked_to_breakpoint();
    test_29_multiple_watchpoints_linked_to_different_breakpoints();
    test_30_multiple_watchpoints_linked_to_same_breakpoint();

    // Category 3: Integration, SysRegs, Reset, SMP, Parallel (31-40)
    test_31_sysreg_dbgbcr_lbn_bt_fields();
    test_32_sysreg_dbgwcr_lbn_wt_fields();
    test_33_reset_clears_linked_debug_state();
    test_34_cpu_step_linked_breakpoint_exception();
    test_35_cpu_memory_access_linked_watchpoint_triggers();
    test_36_cpu_memory_access_linked_watchpoint_instruction_mismatch();
    test_37_syndrome_and_far_correctness();
    test_38_smp_cluster_multicore_debug_isolation();
    test_39_multithreaded_parallel_matching_safety();
    test_40_end_to_end_exception_return_and_recovery();

    std::cout << "\nALL 40 LINKED DEBUG TESTS PASSED (100%)\n";
    return 0;
}
