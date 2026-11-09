// =============================================================================
// WinIOS Phase 52 Stage 39: Bounded AArch64 Architectural Debug Subsystem Tests
// =============================================================================
//
// Suites:
//   1  DebugRegisterArchitectureTests
//   2  HardwareBreakpointMatchingTests
//   3  HardwareWatchpointMatchingTests
//   4  DebugPrivilegeEnforcementTests
//   5  DebugExceptionSyndromeTests
//   6  DebugVectorSelectionTests
//   7  SingleStepExecutionTests
//   8  DebugMmuInteractionTests
//   9  DebugSmpIsolationTests
//  10  DebugSnapshotReplayTests
//  11  DebugReferenceDifferentialTests
//  12  Stage39DebugFuzzTests
//  13  Stage39DebugEnduranceTests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/debug_architecture.h"
#include "cpu/debug_exceptions.h"
#include "cpu/smp_core_state.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"
#include "reference/independent_reference_model.h"
#include "snapshot/smp_snapshot_format.h"
#include "snapshot/smp_snapshot_manager.h"
#include "snapshot/snapshot_serializer.h"
#include "trace/trace_serializer.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace winios;
using namespace winios::reference;
using namespace winios::snapshot;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            g_failed_tests++; \
            return; \
        } \
    } while (0)

#define TEST_PASS(msg) std::cout << "  [PASS] " << msg << "\n"

namespace {

// ------------------------------- Encodings ---------------------------------
constexpr std::uint32_t kNop = 0xD503201Fu;
constexpr std::uint32_t kEret = 0xD69F03E0u;

std::uint32_t enc_movz(unsigned rd, std::uint16_t imm16) {
    return 0xD2800000u | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}
std::uint32_t enc_add_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0x91000000u | ((static_cast<std::uint32_t>(imm12) & 0xFFFu) << 10u) |
           ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}
std::uint32_t enc_str_x(unsigned rt, unsigned rn) {
    return 0xF9000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}
std::uint32_t enc_ldr_x(unsigned rt, unsigned rn) {
    return 0xF9400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}
std::uint32_t enc_str_w(unsigned rt, unsigned rn) {
    return 0xB9000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

std::uint32_t encode_mrs(std::uint8_t op0, std::uint8_t op1, std::uint8_t crn,
                         std::uint8_t crm, std::uint8_t op2, std::uint32_t rt) {
    const std::uint32_t o0 = (op0 == 2) ? 0u : 1u;
    return 0xD5300000u | (o0 << 19u) | (static_cast<std::uint32_t>(op1 & 7u) << 16u) |
           (static_cast<std::uint32_t>(crn & 15u) << 12u) |
           (static_cast<std::uint32_t>(crm & 15u) << 8u) |
           (static_cast<std::uint32_t>(op2 & 7u) << 5u) | (rt & 31u);
}
std::uint32_t encode_msr(std::uint8_t op0, std::uint8_t op1, std::uint8_t crn,
                         std::uint8_t crm, std::uint8_t op2, std::uint32_t rt) {
    const std::uint32_t o0 = (op0 == 2) ? 0u : 1u;
    return 0xD5100000u | (o0 << 19u) | (static_cast<std::uint32_t>(op1 & 7u) << 16u) |
           (static_cast<std::uint32_t>(crn & 15u) << 12u) |
           (static_cast<std::uint32_t>(crm & 15u) << 8u) |
           (static_cast<std::uint32_t>(op2 & 7u) << 5u) | (rt & 31u);
}

// MDSCR_EL1: op0=2 op1=0 CRn=0 CRm=2 op2=2
std::uint32_t mrs_mdscr(unsigned rt) { return encode_mrs(2, 0, 0, 2, 2, rt); }
std::uint32_t msr_mdscr(unsigned rt) { return encode_msr(2, 0, 0, 2, 2, rt); }
// MDCCINT_EL1: op0=2 op1=0 CRn=0 CRm=2 op2=0
std::uint32_t mrs_mdccint(unsigned rt) { return encode_mrs(2, 0, 0, 2, 0, rt); }
std::uint32_t msr_mdccint(unsigned rt) { return encode_msr(2, 0, 0, 2, 0, rt); }
// DBGBVR0_EL1: op0=2 op1=0 CRn=0 CRm=0 op2=4
std::uint32_t mrs_dbgbvr0(unsigned rt) { return encode_mrs(2, 0, 0, 0, 4, rt); }
// DBGWCR0_EL1: op0=2 op1=0 CRn=0 CRm=0 op2=7
std::uint32_t msr_dbgwcr0(unsigned rt) { return encode_msr(2, 0, 0, 0, 7, rt); }

void setup_cpu(CPU& cpu, Memory& mem, unsigned el = 1, std::uint64_t vbar = 0x10000) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(el);
    cpu.set_vbar_el1(vbar);
    cpu.set_vbar_el2(vbar + 0x10000);
    cpu.set_vbar_el3(vbar + 0x20000);
    cpu.set_cpacr_el1(0x00300000ULL);
    for (std::size_t i = 0; i + 4 <= mem.size(); i += 4) {
        mem.write_u32(i, kNop);
    }
}

// Control register builders.
constexpr std::uint32_t bp_control(bool e, unsigned pmc, unsigned bt = 0,
                                   unsigned lbn = 0, unsigned bas = 0xF) {
    return (e ? 1u : 0u) | ((pmc & 3u) << 1u) | ((bas & 0xFu) << 5u) |
           ((lbn & 0xFu) << 16u) | ((bt & 0xFu) << 20u);
}
constexpr std::uint32_t wp_control(bool e, unsigned pmc, unsigned lsc,
                                   unsigned bas = 0xFF, unsigned lbn = 0,
                                   bool wt = false) {
    return (e ? 1u : 0u) | ((pmc & 3u) << 1u) | ((lsc & 3u) << 3u) |
           ((bas & 0xFFu) << 5u) | ((lbn & 0xFu) << 16u) | (wt ? (1u << 20u) : 0u);
}

// Stage 39 identity/displaced 2 MiB block mapping used by MMU interaction tests.
// VA [0, 2MiB) -> PA [kMmuPaBase, kMmuPaBase + 2MiB).
constexpr std::uint64_t kMmuPaBase = 0x200000ULL;

void setup_block_mmu(CPU& cpu, Memory& mem) {
    mem.write_u64(0x1000, 0x2000ULL | 0x3ULL); // L0 -> L1 table
    mem.write_u64(0x2000, 0x3000ULL | 0x3ULL); // L1 -> L2 table
    // L2[0]: 2 MiB block, AF=1, inner-shareable, AttrIndx=0, valid block.
    mem.write_u64(0x3000, kMmuPaBase | (1ULL << 10u) | (3ULL << 8u) | 0x1ULL);
    cpu.set_mair_el1(0xFFULL);
    cpu.get_mmu().set_ttbr0_el1(0x1000ULL);
    cpu.set_tcr_el1(16ULL);
    cpu.set_sctlr_el1(0x00C50839ULL);
}

// Fixed-seed LFSR (xorshift32) for reproducible fuzzing.
struct Lfsr {
    std::uint32_t state;
    explicit Lfsr(std::uint32_t seed) : state(seed ? seed : 1u) {}
    std::uint32_t next() {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return state;
    }
};

} // namespace

// =============================================================================
// Suite 1: DebugRegisterArchitectureTests
// =============================================================================
void run_suite_1_debug_register_architecture() {
    std::cout << "Running Suite 1: DebugRegisterArchitectureTests...\n";

    DebugArchitecture dbg;
    TEST_CHECK(dbg.get_mdscr_el1() == 0, "MDSCR_EL1 resets to 0");
    TEST_CHECK(dbg.get_mdccint_el1() == 0, "MDCCINT_EL1 resets to 0");
    TEST_CHECK(dbg.active_breakpoint_count() == 0, "No active breakpoints at reset");
    TEST_CHECK(dbg.active_watchpoint_count() == 0, "No active watchpoints at reset");
    TEST_CHECK(dbg.debug_event_sequence() == 0, "Debug event sequence resets to 0");
    TEST_CHECK(dbg.pending_event() == nullptr, "No pending debug event at reset");

    TEST_CHECK(kDebugBreakpointCount == 4, "Four breakpoints are modeled");
    TEST_CHECK(kDebugWatchpointCount == 4, "Four watchpoints are modeled");
    TEST_CHECK(DebugArchitecture::breakpoint_count_minus_one() == 3, "BRPs field is 3");
    TEST_CHECK(DebugArchitecture::watchpoint_count_minus_one() == 3, "WRPs field is 3");

    // MDSCR_EL1 subset: SS (bit 0) + MDE (bit 15) only, everything else RAZ/WI.
    dbg.set_mdscr_el1(0xFFFFFFFFFFFFFFFFULL);
    TEST_CHECK(dbg.get_mdscr_el1() == ((1ULL << 15u) | 1ULL), "MDSCR keeps only SS and MDE");
    TEST_CHECK(dbg.monitor_debug_enabled(), "MDE decodes");
    TEST_CHECK(dbg.software_step_enabled(), "SS decodes");

    // MDCCINT_EL1 metadata: bits [3:0] only.
    dbg.set_mdccint_el1(0xFFFFFFFFFFFFFFFFULL);
    TEST_CHECK(dbg.get_mdccint_el1() == 0xFu, "MDCCINT keeps only 4 channel bits");
    TEST_CHECK(dbg.dcc_interrupt_enabled(0) && dbg.dcc_interrupt_enabled(3),
               "DCC 0..3 enabled");
    TEST_CHECK(!dbg.dcc_interrupt_enabled(4), "DCC 4 not modeled");

    // Decode classification.
    const auto off = DebugArchitecture::decode_breakpoint_control(0x0u);
    TEST_CHECK(!off.enabled && off.support == DebugControlSupport::Disabled,
               "E=0 classifies as Disabled");

    const auto simple = DebugArchitecture::decode_breakpoint_control(
        bp_control(true, 1));
    TEST_CHECK(simple.active && simple.support == DebugControlSupport::Supported,
               "Plain E=1 BT=0 PMC=EL1 is Supported/active");
    TEST_CHECK(simple.privilege_mode == DebugPrivilegeMode::EL1, "PMC decodes");

    const auto hmc = DebugArchitecture::decode_breakpoint_control(
        bp_control(true, 0) | (1u << 13u));
    TEST_CHECK(!hmc.active && hmc.hmc &&
               hmc.support == DebugControlSupport::UnsupportedFeature,
               "HMC makes a breakpoint inactive");

    const auto ssc = DebugArchitecture::decode_breakpoint_control(
        bp_control(true, 0) | (1u << 14u));
    TEST_CHECK(!ssc.active && ssc.ssc == 1 &&
               ssc.support == DebugControlSupport::UnsupportedFeature,
               "SSC makes a breakpoint inactive");

    const auto bt2 = DebugArchitecture::decode_breakpoint_control(
        bp_control(true, 0, 2));
    TEST_CHECK(!bt2.active && bt2.support == DebugControlSupport::UnsupportedFeature,
               "BT=2 is not modeled and is inactive");

    const auto bt4 = DebugArchitecture::decode_breakpoint_control(
        bp_control(true, 0, 4));
    TEST_CHECK(!bt4.active && bt4.support == DebugControlSupport::ReservedEncoding,
               "BT=4 is a reserved encoding");

    const auto wp_ok = DebugArchitecture::decode_watchpoint_control(
        wp_control(true, 1, 3));
    TEST_CHECK(wp_ok.active && wp_ok.lsc == WatchpointLoadStoreControl::Both,
               "Watchpoint Both decodes");

    const auto wp_mask = DebugArchitecture::decode_watchpoint_control(
        wp_control(true, 1, 3) | (1u << 24u));
    TEST_CHECK(!wp_mask.active && wp_mask.mask == 1 &&
               wp_mask.support == DebugControlSupport::UnsupportedFeature,
               "Watchpoint MASK != 0 is unsupported and inactive");

    const auto wp_ssc = DebugArchitecture::decode_watchpoint_control(
        wp_control(true, 1, 3) | (2u << 14u));
    TEST_CHECK(!wp_ssc.active && wp_ssc.support == DebugControlSupport::UnsupportedFeature,
               "Watchpoint SSC makes the slot inactive");

    const auto wp_lsc0 = DebugArchitecture::decode_watchpoint_control(
        wp_control(true, 1, 0));
    TEST_CHECK(!wp_lsc0.active &&
               wp_lsc0.support == DebugControlSupport::ReservedEncoding,
               "Watchpoint LSC=0 is reserved");

    TEST_CHECK(std::strcmp(debug_control_support_name(DebugControlSupport::Supported),
                           "Supported") == 0,
               "Support names are stable");

    // Slot activity includes link validation.
    dbg.reset();
    dbg.set_dbgbvr(0, 0x2000);
    dbg.set_dbgbcr(0, bp_control(true, 3, 1, 1)); // linked to slot 1
    TEST_CHECK(!dbg.breakpoint_slot_active(0),
               "Linked breakpoint with disabled target is inactive");
    dbg.set_dbgbcr(1, bp_control(true, 3, 0));
    TEST_CHECK(dbg.breakpoint_slot_active(0) && dbg.breakpoint_slot_active(1),
               "Linked breakpoint with enabled target is active");
    TEST_CHECK(dbg.active_breakpoint_count() == 2, "Two active breakpoints");

    // Out-of-range accessors throw.
    bool threw = false;
    try { (void)dbg.get_dbgbvr(4); } catch (const std::out_of_range&) { threw = true; }
    TEST_CHECK(threw, "get_dbgbvr(4) throws");
    threw = false;
    try { dbg.set_dbgwcr(4, 0); } catch (const std::out_of_range&) { threw = true; }
    TEST_CHECK(threw, "set_dbgwcr(4) throws");

    TEST_PASS("Suite 1: debug register architecture verified");
}

// =============================================================================
// Suite 2: HardwareBreakpointMatchingTests
// =============================================================================
void run_suite_2_hardware_breakpoint_matching() {
    std::cout << "Running Suite 2: HardwareBreakpointMatchingTests...\n";

    DebugArchitecture dbg;
    dbg.set_mdscr_el1(1ULL << 15u);
    dbg.set_dbgbvr(0, 0x4000);
    dbg.set_dbgbcr(0, bp_control(true, 1));

    auto ev = dbg.check_execution_breakpoint(0x4000, 1);
    TEST_CHECK(ev.has_value() && ev->slot_index == 0, "Slot 0 matches at EL1");
    TEST_CHECK(ev->type == DebugEventType::Breakpoint, "Event type is Breakpoint");
    TEST_CHECK(ev->address == 0x4000 && ev->source_el == 1, "Event address/EL recorded");
    TEST_CHECK(dbg.debug_event_sequence() == 1, "Event sequence increments");
    TEST_CHECK(dbg.pending_event() != nullptr &&
               dbg.pending_event()->slot_index == 0,
               "Pending event observes the match");

    TEST_CHECK(!dbg.check_execution_breakpoint(0x4004, 1).has_value(),
               "Different address does not match");

    // MDE gate.
    dbg.set_mdscr_el1(0);
    TEST_CHECK(!dbg.check_execution_breakpoint(0x4000, 1).has_value(),
               "MDE=0 disables breakpoints");
    dbg.set_mdscr_el1(1ULL << 15u);

    // PMC filtering.
    TEST_CHECK(!dbg.check_execution_breakpoint(0x4000, 0).has_value(),
               "PMC=EL1 does not match EL0");
    dbg.set_dbgbcr(0, bp_control(true, 2));
    TEST_CHECK(dbg.check_execution_breakpoint(0x4000, 0).has_value(),
               "PMC=EL0 matches EL0");
    TEST_CHECK(!dbg.check_execution_breakpoint(0x4000, 1).has_value(),
               "PMC=EL0 does not match EL1");
    dbg.set_dbgbcr(0, bp_control(true, 3));
    TEST_CHECK(dbg.check_execution_breakpoint(0x4000, 0).has_value() &&
               dbg.check_execution_breakpoint(0x4000, 1).has_value(),
               "PMC=EL0|EL1 matches both");
    dbg.set_dbgbcr(0, bp_control(true, 0));
    TEST_CHECK(dbg.check_execution_breakpoint(0x4000, 2).has_value(),
               "PMC=00 matches EL2");

    // Slot priority: lowest slot wins.
    dbg.set_dbgbvr(1, 0x4000);
    dbg.set_dbgbcr(1, bp_control(true, 0));
    ev = dbg.check_execution_breakpoint(0x4000, 1);
    TEST_CHECK(ev.has_value() && ev->slot_index == 0, "Slot 0 has priority");
    dbg.set_dbgbcr(0, 0);
    ev = dbg.check_execution_breakpoint(0x4000, 1);
    TEST_CHECK(ev.has_value() && ev->slot_index == 1, "Slot 1 matches when slot 0 off");

    // Unsupported encodings never fire.
    dbg.reset();
    dbg.set_mdscr_el1(1ULL << 15u);
    dbg.set_dbgbvr(0, 0x4000);

    dbg.set_dbgbcr(0, bp_control(true, 0) | (1u << 13u)); // HMC
    TEST_CHECK(!dbg.check_execution_breakpoint(0x4000, 1).has_value(),
               "HMC breakpoint does not fire");

    dbg.set_dbgbcr(0, bp_control(true, 0) | (3u << 14u)); // SSC
    TEST_CHECK(!dbg.check_execution_breakpoint(0x4000, 1).has_value(),
               "SSC breakpoint does not fire");

    dbg.set_dbgbcr(0, bp_control(true, 0, 2)); // BT=2
    TEST_CHECK(!dbg.check_execution_breakpoint(0x4000, 1).has_value(),
               "BT=2 breakpoint does not fire");

    dbg.set_dbgbcr(0, bp_control(true, 0, 4)); // reserved BT
    TEST_CHECK(!dbg.check_execution_breakpoint(0x4000, 1).has_value(),
               "Reserved BT does not fire");

    dbg.set_dbgbcr(0, bp_control(true, 0, 3)); // BT=3 link target only
    TEST_CHECK(!dbg.check_execution_breakpoint(0x4000, 1).has_value(),
               "BT=3 never fires as a top-level breakpoint");

    // Linked breakpoint.
    dbg.reset();
    dbg.set_mdscr_el1(1ULL << 15u);
    dbg.set_dbgbvr(0, 0x4000);
    dbg.set_dbgbcr(0, bp_control(true, 0, 1, 1));
    dbg.set_dbgbvr(1, 0x4000);
    dbg.set_dbgbcr(1, bp_control(true, 0));
    ev = dbg.check_execution_breakpoint(0x4000, 1);
    TEST_CHECK(ev.has_value() && ev->slot_index == 0,
               "Linked breakpoint fires when link target matches");
    dbg.set_dbgbcr(1, 0); // disable target
    TEST_CHECK(!dbg.check_execution_breakpoint(0x4000, 1).has_value(),
               "Linked breakpoint suppressed when target disabled");
    dbg.set_dbgbcr(1, bp_control(true, 0));
    dbg.set_dbgbvr(1, 0x8000); // target address mismatch
    TEST_CHECK(!dbg.check_execution_breakpoint(0x4000, 1).has_value(),
               "Linked breakpoint suppressed when target address differs");

    // Self-referential link.
    dbg.set_dbgbvr(0, 0x4000);
    dbg.set_dbgbcr(0, bp_control(true, 0, 1, 0));
    TEST_CHECK(!dbg.check_execution_breakpoint(0x4000, 1).has_value(),
               "Self-linked breakpoint is inactive");

    TEST_PASS("Suite 2: hardware breakpoint matching verified");
}

// =============================================================================
// Suite 3: HardwareWatchpointMatchingTests
// =============================================================================
void run_suite_3_hardware_watchpoint_matching() {
    std::cout << "Running Suite 3: HardwareWatchpointMatchingTests...\n";

    DebugArchitecture dbg;
    dbg.set_mdscr_el1(1ULL << 15u);
    dbg.set_dbgwvr(0, 0x5000);
    dbg.set_dbgwcr(0, wp_control(true, 3, 2)); // store only, all bytes

    auto ev = dbg.check_data_watchpoint(0x5000, 8, DebugAccessType::Write, 1);
    TEST_CHECK(ev.has_value() && ev->slot_index == 0, "Store watchpoint matches");
    TEST_CHECK(ev->access == DebugAccessType::Write && ev->access_size == 8,
               "Event records write access and size");
    TEST_CHECK(!dbg.check_data_watchpoint(0x5000, 8, DebugAccessType::Read, 1).has_value(),
               "Store-only watchpoint ignores reads");

    dbg.set_dbgwcr(0, wp_control(true, 3, 1)); // load only
    TEST_CHECK(dbg.check_data_watchpoint(0x5000, 8, DebugAccessType::Read, 1).has_value(),
               "Load watchpoint matches reads");
    TEST_CHECK(!dbg.check_data_watchpoint(0x5000, 8, DebugAccessType::Write, 1).has_value(),
               "Load-only watchpoint ignores writes");

    dbg.set_dbgwcr(0, wp_control(true, 3, 3)); // both
    TEST_CHECK(dbg.check_data_watchpoint(0x5000, 8, DebugAccessType::Read, 1).has_value() &&
               dbg.check_data_watchpoint(0x5000, 8, DebugAccessType::Write, 1).has_value(),
               "Both matches reads and writes");

    // BAS byte granularity.
    dbg.set_dbgwvr(0, 0x5008);
    dbg.set_dbgwcr(0, wp_control(true, 3, 3, 0x01)); // byte 0 only
    TEST_CHECK(dbg.check_data_watchpoint(0x5008, 1, DebugAccessType::Read, 1).has_value(),
               "BAS byte 0 matches address 0x5008");
    TEST_CHECK(!dbg.check_data_watchpoint(0x5009, 1, DebugAccessType::Read, 1).has_value(),
               "BAS byte 0 does not match address 0x5009");
    TEST_CHECK(dbg.check_data_watchpoint(0x5008, 4, DebugAccessType::Read, 1).has_value(),
               "Multi-byte access overlapping BAS byte matches");

    // Access starting before the watched 8-byte region but overlapping it.
    dbg.set_dbgwvr(0, 0x5000);
    dbg.set_dbgwcr(0, wp_control(true, 3, 3, 0x0F)); // low nibble of region 0x5000
    TEST_CHECK(dbg.check_data_watchpoint(0x4FFE, 4, DebugAccessType::Read, 1).has_value(),
               "Access [0x4FFE,0x5002) overlaps watched bytes");
    TEST_CHECK(!dbg.check_data_watchpoint(0x4FF8, 4, DebugAccessType::Read, 1).has_value(),
               "Access [0x4FF8,0x4FFC) is outside the watched region");

    // MASK != 0 must make the slot inactive (Stage 39 fix).
    DebugArchitecture masked;
    masked.set_mdscr_el1(1ULL << 15u);
    masked.set_dbgwvr(0, 0x5000);
    masked.set_dbgwcr(0, wp_control(true, 3, 3) | (1u << 24u));
    TEST_CHECK(!masked.check_data_watchpoint(0x5000, 8, DebugAccessType::Read, 1).has_value(),
               "Non-zero MASK suppresses the watchpoint");
    TEST_CHECK(masked.active_watchpoint_count() == 0, "Masked slot is inactive");

    // HMC / SSC / reserved LSC.
    DebugArchitecture unsupported;
    unsupported.set_mdscr_el1(1ULL << 15u);
    unsupported.set_dbgwvr(0, 0x5000);
    unsupported.set_dbgwcr(0, wp_control(true, 3, 3) | (1u << 13u));
    TEST_CHECK(!unsupported.check_data_watchpoint(0x5000, 8, DebugAccessType::Read, 1).has_value(),
               "HMC watchpoint inactive");
    unsupported.set_dbgwcr(0, wp_control(true, 3, 3) | (1u << 15u));
    TEST_CHECK(!unsupported.check_data_watchpoint(0x5000, 8, DebugAccessType::Read, 1).has_value(),
               "SSC watchpoint inactive");
    unsupported.set_dbgwcr(0, wp_control(true, 3, 0));
    TEST_CHECK(!unsupported.check_data_watchpoint(0x5000, 8, DebugAccessType::Read, 1).has_value(),
               "LSC=00 watchpoint inactive");

    // PAC filtering.
    dbg.set_dbgwcr(0, wp_control(true, 1, 3)); // EL1 only
    TEST_CHECK(dbg.check_data_watchpoint(0x5000, 8, DebugAccessType::Read, 1).has_value(),
               "PAC=EL1 matches EL1");
    TEST_CHECK(!dbg.check_data_watchpoint(0x5000, 8, DebugAccessType::Read, 0).has_value(),
               "PAC=EL1 does not match EL0");

    // Linked watchpoint (WT=1).
    DebugArchitecture linked;
    linked.set_mdscr_el1(1ULL << 15u);
    linked.set_dbgbvr(0, 0x2000);
    linked.set_dbgbcr(0, bp_control(true, 3));
    linked.set_dbgwvr(0, 0x6000);
    linked.set_dbgwcr(0, wp_control(true, 3, 3, 0xFF, 0, true));
    TEST_CHECK(linked.check_data_watchpoint(0x6000, 8, DebugAccessType::Write, 1, 0x2000).has_value(),
               "WT linked watchpoint matches with matching instruction PC");
    TEST_CHECK(!linked.check_data_watchpoint(0x6000, 8, DebugAccessType::Write, 1, 0x2004).has_value(),
               "WT linked watchpoint rejects mismatching instruction PC");
    linked.set_dbgbcr(0, 0);
    TEST_CHECK(!linked.check_data_watchpoint(0x6000, 8, DebugAccessType::Write, 1, 0x2000).has_value(),
               "WT linked watchpoint requires an enabled breakpoint");

    // MDE gate and slot priority.
    dbg.set_mdscr_el1(0);
    TEST_CHECK(!dbg.check_data_watchpoint(0x5000, 8, DebugAccessType::Read, 1).has_value(),
               "MDE=0 disables watchpoints");
    dbg.set_mdscr_el1(1ULL << 15u);
    dbg.set_dbgwvr(1, 0x5000);
    dbg.set_dbgwcr(1, wp_control(true, 3, 3));
    ev = dbg.check_data_watchpoint(0x5000, 8, DebugAccessType::Read, 1);
    TEST_CHECK(ev.has_value() && ev->slot_index == 0, "Slot 0 priority for watchpoints");

    TEST_PASS("Suite 3: hardware watchpoint matching verified");
}

// =============================================================================
// Suite 4: DebugPrivilegeEnforcementTests
// =============================================================================
void run_suite_4_debug_privilege_enforcement() {
    std::cout << "Running Suite 4: DebugPrivilegeEnforcementTests...\n";

    // EL0 access to debug registers is undefined.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 0);
        cpu.set_program_counter(0x2000);
        cpu.write_register(1, 0x1);
        mem.write_u32(0x2000, msr_mdscr(1));
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x00u, "EL0 MSR MDSCR is undefined");
        TEST_CHECK(cpu.get_elr_el1() == 0x2000, "EL0 MSR fault ELR is the instruction");
    }
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 0);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, mrs_dbgbvr0(0));
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x00u, "EL0 MRS DBGBVR0 is undefined");
    }
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 0);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, msr_mdccint(1)); // Stage 39 register
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x00u, "EL0 MSR MDCCINT is undefined");
    }

    // EL1 access is permitted and effective.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1);
        cpu.set_program_counter(0x2000);
        cpu.write_register(1, 0xFFFFULL);
        mem.write_u32(0x2000, msr_mdccint(1));
        mem.write_u32(0x2004, mrs_mdccint(0));
        cpu.step(mem);
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x00u, "EL1 MDCCINT access does not fault");
        TEST_CHECK(cpu.read_register(0) == 0xFu, "MDCCINT read-back keeps bits [3:0]");
        TEST_CHECK(cpu.get_debug_arch().dcc_interrupt_enabled(3),
                   "MDCCINT state visible through debug architecture");
    }

    // TDA traps to EL2 (source EL1) and EL3 (source EL2).
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1);
        cpu.set_mdcr_el2(1ULL << 9u);
        cpu.set_program_counter(0x2000);
        cpu.write_register(1, 0x1);
        mem.write_u32(0x2000, msr_mdscr(1));
        cpu.step(mem);
        TEST_CHECK(cpu.get_current_el() == 2, "MDCR_EL2.TDA routes debug access to EL2");
        TEST_CHECK((cpu.get_esr_el2() >> 26u) == 0x18u, "TDA trap uses EC 0x18");
        TEST_CHECK(cpu.get_elr_el2() == 0x2000, "TDA trap ELR is the instruction");
    }
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 2);
        cpu.set_mdcr_el3(1ULL << 9u);
        cpu.set_program_counter(0x2000);
        cpu.write_register(1, 0x1);
        mem.write_u32(0x2000, msr_mdscr(1));
        cpu.step(mem);
        TEST_CHECK(cpu.get_current_el() == 3, "MDCR_EL3.TDA routes debug access to EL3");
    }

    // Debug target EL rules match the independent reference.
    for (unsigned src = 0; src <= 3; ++src) {
        for (unsigned mask = 0; mask < 8; ++mask) {
            const std::uint64_t mdcr2 = (mask & 1u) ? (1ULL << 8u) : 0ULL;
            const std::uint64_t mdcr3 = (mask & 2u) ? (1ULL << 8u) : 0ULL;
            const std::uint64_t hcr = (mask & 4u) ? (1ULL << 27u) : 0ULL;
            const unsigned prod = determine_debug_target_el(src, mdcr2, mdcr3, hcr);
            const unsigned ref = ReferenceDebugModel::target_el(src, mdcr2, mdcr3, hcr);
            TEST_CHECK(prod == ref, "Debug target EL must match the reference model");
        }
    }

    // Software-step can be masked by PSTATE.D when targeting the same EL.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_add_imm(0, 0, 1));
        cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | 1ULL);
        cpu.set_daif(0x8u); // D set
        cpu.set_pstate_ss(true);
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26u) != 0x33u,
                   "PSTATE.D masks a same-EL software-step exception");
        TEST_CHECK(cpu.get_program_counter() == 0x2004, "Instruction executed normally");
    }

    // MDE gating applies to software step as well.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_add_imm(0, 0, 1));
        cpu.get_debug_arch().set_mdscr_el1(1ULL); // SS=1, MDE=0
        cpu.set_daif(0);
        cpu.set_pstate_ss(true);
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26u) != 0x33u, "MDE=0 suppresses software step");
    }

    TEST_PASS("Suite 4: debug privilege enforcement verified");
}

// =============================================================================
// Suite 5: DebugExceptionSyndromeTests
// =============================================================================
void run_suite_5_debug_exception_syndrome() {
    std::cout << "Running Suite 5: DebugExceptionSyndromeTests...\n";

    {
        DebugExceptionEvent ev;
        ev.kind = DebugExceptionKind::HardwareBreakpoint;
        ev.source_el = 1;
        ev.instruction_address = 0x4000;
        const auto ex = create_debug_synchronous_exception(ev, 1);
        TEST_CHECK((ex.esr() >> 26u) == 0x31u, "Same-EL breakpoint EC is 0x31");
        TEST_CHECK(ex.esr() == 0xC6000000u, "Breakpoint ESR is EC|IL with ISS=0");
        TEST_CHECK(!ex.has_fault_address, "Breakpoint has no FAR");
    }
    {
        DebugExceptionEvent ev;
        ev.kind = DebugExceptionKind::HardwareBreakpoint;
        ev.source_el = 0;
        ev.instruction_address = 0x4000;
        const auto ex = create_debug_synchronous_exception(ev, 1);
        TEST_CHECK((ex.esr() >> 26u) == 0x30u, "Lower-EL breakpoint EC is 0x30");
        TEST_CHECK(ex.target_el == 1 && !ex.same_el, "Lower-EL target/same flags");
    }
    {
        DebugExceptionEvent ev;
        ev.kind = DebugExceptionKind::HardwareWatchpoint;
        ev.source_el = 1;
        ev.fault_address = 0x5000;
        ev.is_write = true;
        ev.instruction_address = 0x2000;
        const auto ex = create_debug_synchronous_exception(ev, 1);
        TEST_CHECK((ex.esr() >> 26u) == 0x35u, "Same-EL watchpoint EC is 0x35");
        TEST_CHECK((ex.esr() & 0x3Fu) == 0x22u, "Watchpoint DFSC is 0x22");
        TEST_CHECK((ex.esr() & 0x40u) != 0, "WnR is set for stores");
        TEST_CHECK(ex.has_fault_address && ex.fault_address == 0x5000,
                   "Watchpoint FAR holds the data address");
        TEST_CHECK(ex.preferred_return == 0x2000, "Watchpoint returns to the instruction");
    }
    {
        DebugExceptionEvent ev;
        ev.kind = DebugExceptionKind::HardwareWatchpoint;
        ev.source_el = 0;
        ev.fault_address = 0x5000;
        ev.is_write = false;
        const auto ex = create_debug_synchronous_exception(ev, 1);
        TEST_CHECK((ex.esr() >> 26u) == 0x34u, "Lower-EL watchpoint EC is 0x34");
        TEST_CHECK((ex.esr() & 0x40u) == 0, "WnR is clear for loads");
    }
    {
        DebugExceptionEvent ev;
        ev.kind = DebugExceptionKind::SoftwareStep;
        ev.source_el = 1;
        ev.instruction_address = 0x2000;
        ev.is_exclusive = true;
        const auto ex = create_debug_synchronous_exception(ev, 1);
        TEST_CHECK((ex.esr() >> 26u) == 0x33u, "Same-EL software step EC is 0x33");
        TEST_CHECK((ex.esr() & 0x3Fu) == 0x22u, "Software step DFSC is 0x22");
        TEST_CHECK((ex.esr() & 0x40u) != 0, "EX bit is reported");
        TEST_CHECK(!ex.has_fault_address, "Software step has no FAR");
    }
    {
        DebugExceptionEvent ev;
        ev.kind = DebugExceptionKind::SoftwareStep;
        ev.source_el = 0;
        const auto ex = create_debug_synchronous_exception(ev, 1);
        TEST_CHECK((ex.esr() >> 26u) == 0x32u, "Lower-EL software step EC is 0x32");
    }
    {
        DebugExceptionEvent ev;
        ev.kind = DebugExceptionKind::SoftwareBreakpoint;
        ev.source_el = 1;
        ev.comment = 0xBEEF;
        const auto ex = create_debug_synchronous_exception(ev, 1);
        TEST_CHECK((ex.esr() >> 26u) == 0x3Cu, "BRK uses EC 0x3C");
        TEST_CHECK((ex.esr() & 0xFFFFu) == 0xBEEFu, "BRK imm16 is preserved");
    }

    // Differential: every debug syndrome matches the independent reference.
    for (unsigned same = 0; same <= 1; ++same) {
        const bool s = same != 0;
        for (unsigned write = 0; write <= 1; ++write) {
            TEST_CHECK(ReferenceDebugModel::breakpoint_esr(s) ==
                       (((s ? 0x31u : 0x30u) << 26u) | (1u << 25u)),
                       "Reference breakpoint syndrome stable");
            TEST_CHECK(ReferenceDebugModel::watchpoint_esr(s, write != 0) ==
                       (((s ? 0x35u : 0x34u) << 26u) | (1u << 25u) |
                        (write ? (1u << 6u) : 0u) | 0x22u),
                       "Reference watchpoint syndrome formula");
            TEST_CHECK(ReferenceDebugModel::software_step_esr(s, write != 0) ==
                       (((s ? 0x33u : 0x32u) << 26u) | (1u << 25u) |
                        (write ? (1u << 6u) : 0u) | 0x22u),
                       "Reference software-step syndrome formula");
        }
    }

    TEST_PASS("Suite 5: debug exception syndromes verified");
}

// =============================================================================
// Suite 6: DebugVectorSelectionTests
// =============================================================================
void run_suite_6_debug_vector_selection() {
    std::cout << "Running Suite 6: DebugVectorSelectionTests...\n";

    TEST_CHECK(determine_debug_vector_offset(1, 1, true) == 0x200u, "Same EL SPx -> 0x200");
    TEST_CHECK(determine_debug_vector_offset(1, 1, false) == 0x000u, "Same EL SP0 -> 0x000");
    TEST_CHECK(determine_debug_vector_offset(0, 1, true) == 0x400u, "Lower EL -> 0x400");
    for (unsigned src = 0; src <= 3; ++src) {
        for (unsigned tgt = 0; tgt <= 3; ++tgt) {
            for (unsigned sp = 0; sp <= 1; ++sp) {
                const auto prod = determine_debug_vector_offset(src, tgt, sp != 0);
                const auto ref = ReferenceDebugModel::vector_offset(src, tgt, sp != 0);
                TEST_CHECK(prod == ref, "Debug vector offset matches reference");
            }
        }
    }

    // End-to-end SPx selection.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_sp_sel(true);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, kNop);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, bp_control(true, 3));
        cpu.step(mem);
        TEST_CHECK(cpu.get_program_counter() == 0x10200ULL,
                   "Breakpoint at EL1 SPx enters VBAR_EL1 + 0x200");
    }
    // End-to-end SP0 selection.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_sp_sel(false);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, kNop);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, bp_control(true, 3));
        cpu.step(mem);
        TEST_CHECK(cpu.get_program_counter() == 0x10000ULL,
                   "Breakpoint at EL1 SP0 enters VBAR_EL1 + 0x000");
    }
    // Lower-EL (EL0 -> EL1) selection.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 0, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, kNop);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, bp_control(true, 2));
        cpu.step(mem);
        TEST_CHECK(cpu.get_current_el() == 1, "EL0 breakpoint escalates to EL1");
        TEST_CHECK(cpu.get_program_counter() == 0x10400ULL,
                   "Lower-EL breakpoint enters VBAR_EL1 + 0x400");
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x30u, "Lower-EL breakpoint EC 0x30");
    }
    // TDE routing to EL2.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_vbar_el2(0x20000);
        cpu.set_mdcr_el2(1ULL << 8u);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, kNop);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, bp_control(true, 3));
        cpu.step(mem);
        TEST_CHECK(cpu.get_current_el() == 2, "TDE routes breakpoint to EL2");
        TEST_CHECK(cpu.get_program_counter() == 0x20400ULL,
                   "EL1->EL2 breakpoint enters VBAR_EL2 + 0x400");
    }

    TEST_PASS("Suite 6: debug vector selection verified");
}

// =============================================================================
// Suite 7: SingleStepExecutionTests
// =============================================================================
void run_suite_7_single_step_execution() {
    std::cout << "Running Suite 7: SingleStepExecutionTests...\n";

    // Basic retirement step.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        cpu.write_register(0, 0);
        mem.write_u32(0x2000, enc_add_imm(0, 0, 1));
        cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | 1ULL);
        cpu.set_daif(0);
        cpu.set_pstate_ss(true);
        cpu.step(mem);
        TEST_CHECK(cpu.read_register(0) == 1, "Stepped instruction executed");
        TEST_CHECK(cpu.get_elr_el1() == 0x2004, "Step ELR is next PC");
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x33u, "Retirement step EC 0x33");
        TEST_CHECK((cpu.get_esr_el1() & 0x3Fu) == 0x22u, "Retirement step DFSC 0x22");
        TEST_CHECK(!cpu.get_pstate_ss(), "PSTATE.SS cleared on step entry");
        TEST_CHECK(cpu.get_program_counter() == 0x10200ULL,
                   "Step enters VBAR_EL1 + 0x200");
        TEST_CHECK(cpu.get_debug_arch().debug_event_sequence() >= 1,
                   "Software-step event recorded");
        const auto* pe = cpu.get_debug_arch().pending_event();
        TEST_CHECK(pe != nullptr && pe->type == DebugEventType::SoftwareStep,
                   "Pending event is SoftwareStep");
    }
    // Active-pending: SS enabled but PSTATE.SS clear steps before executing.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        cpu.write_register(5, 0);
        mem.write_u32(0x2000, enc_movz(5, 0x1234));
        cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | 1ULL);
        cpu.set_daif(0);
        cpu.set_pstate_ss(false);
        cpu.step(mem);
        TEST_CHECK(cpu.read_register(5) == 0, "Active-pending step does not execute");
        TEST_CHECK(cpu.get_elr_el1() == 0x2000, "Active-pending ELR is the current PC");
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x33u, "Active-pending EC 0x33");
    }
    // Faulting instruction is not stepped.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, 0x00000000u); // undefined
        cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | 1ULL);
        cpu.set_daif(0);
        cpu.set_pstate_ss(true);
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x00u, "Fault reports EC 0x00 not step");
        TEST_CHECK(cpu.get_elr_el1() == 0x2000, "Faulting ELR is the instruction");
    }
    // SVC is not stepped.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, 0xD4000001u | (42u << 5u)); // SVC #42
        cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | 1ULL);
        cpu.set_daif(0);
        cpu.set_pstate_ss(true);
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x15u, "SVC wins over software step");
    }
    // Exclusive step reports EX.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        cpu.write_register(1, 0x5000);
        mem.write_u64(0x5000, 0x99);
        mem.write_u32(0x2000, 0xC85F7C20u); // LDXR X0, [X1]
        cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | 1ULL);
        cpu.set_daif(0);
        cpu.set_pstate_ss(true);
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x33u, "Exclusive step EC 0x33");
        TEST_CHECK((cpu.get_esr_el1() & 0x40u) != 0, "EX bit set for exclusive step");
    }
    // ERET restores PSTATE.SS from SPSR_EL1.SS.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, kEret);
        cpu.set_elr_el1(0x4000);
        cpu.set_spsr_el1(1u << 21u);
        cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | 1ULL);
        cpu.step(mem);
        TEST_CHECK(cpu.get_current_el() == 0 && cpu.get_program_counter() == 0x4000,
                   "ERET returns to EL0/ELR");
        TEST_CHECK(cpu.get_pstate_ss(), "ERET restores PSTATE.SS from SPSR");
    }
    // Consecutive steps produce one event per instruction.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_add_imm(0, 0, 1));
        mem.write_u32(0x2004, enc_add_imm(0, 0, 1));
        cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | 1ULL);
        cpu.set_daif(0);
        cpu.set_pstate_ss(true);
        const auto before = cpu.get_debug_arch().debug_event_sequence();
        cpu.step(mem);
        cpu.set_program_counter(cpu.get_elr_el1());
        cpu.set_daif(0);
        cpu.set_pstate_ss(true);
        cpu.step(mem);
        TEST_CHECK(cpu.read_register(0) == 2, "Both instructions retired");
        TEST_CHECK(cpu.get_debug_arch().debug_event_sequence() == before + 2,
                   "Exactly one step event per instruction");
    }

    TEST_PASS("Suite 7: single-step execution verified");
}

// =============================================================================
// Suite 8: DebugMmuInteractionTests
// =============================================================================
void run_suite_8_debug_mmu_interaction() {
    std::cout << "Running Suite 8: DebugMmuInteractionTests...\n";

    // 1. Breakpoints match the virtual address, before translation.  The MMU
    //    maps VA 0x2000 -> PA 0x202000; a breakpoint at VA 0x2000 must fire.
    {
        CPU cpu;
        Memory mem(0x400000);
        setup_cpu(cpu, mem, 1, 0x10000);
        setup_block_mmu(cpu, mem);
        mem.write_u32(kMmuPaBase + 0x2000, kNop);
        cpu.set_program_counter(0x2000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, bp_control(true, 3));
        cpu.step(mem);
        TEST_CHECK(cpu.get_current_el() == 1 && cpu.get_program_counter() == 0x10200ULL,
                   "Breakpoint on a translated VA fires");
        TEST_CHECK(cpu.get_elr_el1() == 0x2000, "Breakpoint ELR is the VA");
    }

    // 2. A breakpoint is evaluated before instruction translation faults.
    {
        CPU cpu;
        Memory mem(0x400000);
        setup_cpu(cpu, mem, 1, 0x10000);
        setup_block_mmu(cpu, mem);
        cpu.set_program_counter(0x400000); // outside the mapped 2 MiB block
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x400000);
        cpu.get_debug_arch().set_dbgbcr(0, bp_control(true, 3));
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x31u,
                   "Breakpoint wins over an instruction translation fault");
        TEST_CHECK(cpu.get_program_counter() == 0x10200ULL,
                   "Breakpoint vector entered, not instruction abort");
    }

    // 3. Watchpoints are evaluated after translation: an unmapped store raises a
    //    data abort, never a watchpoint.
    {
        CPU cpu;
        Memory mem(0x400000);
        setup_cpu(cpu, mem, 1, 0x10000);
        setup_block_mmu(cpu, mem);
        cpu.set_program_counter(0x2000);
        cpu.write_register(0, 0x77);
        cpu.write_register(1, 0x400000); // unmapped VA
        mem.write_u32(kMmuPaBase + 0x2000, enc_str_x(0, 1));
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x400000);
        cpu.get_debug_arch().set_dbgwcr(0, wp_control(true, 3, 2));
        const auto before = cpu.get_debug_arch().debug_event_sequence();
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x25u,
                   "Unmapped store reports a data abort");
        TEST_CHECK(cpu.get_far_el1() == 0x400000, "Data abort FAR is the VA");
        TEST_CHECK(cpu.get_debug_arch().debug_event_sequence() == before,
                   "No watchpoint event is recorded for a faulting access");
    }

    // 4. A watchpoint on a translated VA fires; the store is not committed
    //    because the debug exception is taken first.
    {
        CPU cpu;
        Memory mem(0x400000);
        setup_cpu(cpu, mem, 1, 0x10000);
        setup_block_mmu(cpu, mem);
        mem.write_u64(kMmuPaBase + 0x5000, 0x1111);
        cpu.set_program_counter(0x2000);
        cpu.write_register(0, 0x2222);
        cpu.write_register(1, 0x5000);
        mem.write_u32(kMmuPaBase + 0x2000, enc_str_x(0, 1));
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, wp_control(true, 3, 2));
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x35u,
                   "Watchpoint on a translated store fires");
        TEST_CHECK(cpu.get_far_el1() == 0x5000, "Watchpoint FAR is the VA");
        TEST_CHECK(mem.read_u64(kMmuPaBase + 0x5000) == 0x1111ULL,
                   "Store is not committed when the watchpoint fires");

        // With debug disabled the same store commits.
        cpu.get_debug_arch().set_mdscr_el1(0);
        cpu.set_program_counter(0x2000);
        cpu.set_esr_el1(0);
        cpu.set_daif(0);
        cpu.step(mem);
        TEST_CHECK(mem.read_u64(kMmuPaBase + 0x5000) == 0x2222ULL,
                   "Store commits when MDE=0");
    }

    // 5. A load watchpoint does not modify the destination register.
    {
        CPU cpu;
        Memory mem(0x400000);
        setup_cpu(cpu, mem, 1, 0x10000);
        setup_block_mmu(cpu, mem);
        mem.write_u64(kMmuPaBase + 0x5000, 0xABCD);
        cpu.set_program_counter(0x2000);
        cpu.write_register(0, 0x55);
        cpu.write_register(1, 0x5000);
        mem.write_u32(kMmuPaBase + 0x2000, enc_ldr_x(0, 1));
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, wp_control(true, 3, 1));
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x35u, "Load watchpoint fires");
        TEST_CHECK((cpu.get_esr_el1() & 0x40u) == 0, "Load watchpoint WnR=0");
        TEST_CHECK(cpu.read_register(0) == 0x55, "Load result not written on debug exception");
    }

    TEST_PASS("Suite 8: debug/MMU interaction verified");
}

// =============================================================================
// Suite 9: DebugSmpIsolationTests
// =============================================================================
void run_suite_9_debug_smp_isolation() {
    std::cout << "Running Suite 9: DebugSmpIsolationTests...\n";

    // Per-core debug registers are independent.
    {
        std::array<std::unique_ptr<CPU>, 4> cores;
        std::array<std::unique_ptr<Memory>, 4> mems;
        for (std::size_t i = 0; i < cores.size(); ++i) {
            cores[i] = std::make_unique<CPU>();
            mems[i] = std::make_unique<Memory>(0x40000);
            setup_cpu(*cores[i], *mems[i], 1, 0x10000);
            cores[i]->set_program_counter(0x2000);
            mems[i]->write_u32(0x2000, kNop);
            cores[i]->get_debug_arch().set_mdscr_el1(1ULL << 15u);
            cores[i]->get_debug_arch().set_dbgbvr(0, 0x1000 * (i + 1));
            cores[i]->get_debug_arch().set_dbgbcr(0, bp_control(true, 3));
        }
        // Only core 1's breakpoint address matches the executed PC.
        cores[1]->get_debug_arch().set_dbgbvr(0, 0x2000);

        cores[0]->step(*mems[0]);
        TEST_CHECK((cores[0]->get_esr_el1() >> 26u) != 0x31u,
                   "Core 0 does not take core 1's breakpoint");
        TEST_CHECK(cores[0]->get_program_counter() == 0x2004,
                   "Core 0 executes normally");

        cores[1]->step(*mems[1]);
        TEST_CHECK((cores[1]->get_esr_el1() >> 26u) == 0x31u,
                   "Core 1 takes its own breakpoint");

        cores[2]->step(*mems[2]);
        TEST_CHECK((cores[2]->get_esr_el1() >> 26u) != 0x31u,
                   "Core 2 does not take core 1's breakpoint");
    }

    // SmpCoreState capture/apply/hash/equality include debug state.
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1);
        cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | 1ULL);
        cpu.get_debug_arch().set_mdccint_el1(0x5);
        cpu.get_debug_arch().set_dbgbvr(2, 0xDEADBEEF);
        cpu.get_debug_arch().set_dbgbcr(2, bp_control(true, 1, 0, 0, 0xF));
        cpu.get_debug_arch().set_dbgwvr(3, 0xCAFEBABE);
        cpu.get_debug_arch().set_dbgwcr(3, wp_control(true, 2, 3, 0x0F));

        SmpCoreState a;
        a.capture_from_cpu(cpu);
        TEST_CHECK(a.debug.mdscr_el1 == ((1ULL << 15u) | 1ULL), "MDSCR captured");
        TEST_CHECK(a.debug.mdccint_el1 == 0x5, "MDCCINT captured");
        TEST_CHECK(a.debug.breakpoint_value[2] == 0xDEADBEEFULL, "DBGBVR captured");
        TEST_CHECK(a.debug.watchpoint_value[3] == 0xCAFEBABEULL, "DBGWVR captured");

        SmpCoreState b = a;
        TEST_CHECK(a == b && a.compute_canonical_hash() == b.compute_canonical_hash(),
                   "Identical debug state hashes equally");

        b.debug.watchpoint_control[3] ^= (1u << 3u);
        TEST_CHECK(a != b, "Equality observes debug control changes");
        TEST_CHECK(a.compute_canonical_hash() != b.compute_canonical_hash(),
                   "Hash observes debug control changes");

        CPU restored;
        Memory rmem(0x40000);
        setup_cpu(restored, rmem, 1);
        a.apply_to_cpu(restored);
        TEST_CHECK(restored.get_debug_arch().get_mdscr_el1() == ((1ULL << 15u) | 1ULL),
                   "MDSCR applied");
        TEST_CHECK(restored.get_debug_arch().get_mdccint_el1() == 0x5,
                   "MDCCINT applied");
        TEST_CHECK(restored.get_debug_arch().get_dbgbvr(2) == 0xDEADBEEFULL,
                   "DBGBVR applied");
        TEST_CHECK(restored.get_debug_arch().get_dbgwcr(3) ==
                   wp_control(true, 2, 3, 0x0F), "DBGWCR applied");
    }

    // Scheduler canonical hash is sensitive to per-core debug state.
    {
        auto shared_mem = std::make_shared<SmpSharedMemory>(0x40000);
        SmpScheduler sched(shared_mem, 4, 100);
        for (std::size_t i = 0; i < 4; ++i) {
            auto cpu = std::make_shared<CPU>();
            cpu->reset();
            cpu->set_cpacr_el1(0x00300000ULL);
            cpu->get_debug_arch().set_mdscr_el1(1ULL << 15u);
            cpu->get_debug_arch().set_dbgbvr(0, 0x3000 + i * 0x10);
            cpu->get_debug_arch().set_dbgbcr(0, bp_control(true, 3));
            sched.register_core(i, cpu, CoreLifecycleState::Runnable);
            sched.get_slot(i).state.capture_from_cpu(*cpu);
        }
        const auto h1 = sched.compute_canonical_multicore_hash();
        sched.get_slot(2).cpu->get_debug_arch().set_dbgwcr(0, wp_control(true, 3, 3));
        sched.get_slot(2).state.capture_from_cpu(*sched.get_slot(2).cpu);
        const auto h2 = sched.compute_canonical_multicore_hash();
        TEST_CHECK(h1 != h2, "Cluster hash changes when a core's watchpoint changes");

        sched.get_slot(2).cpu->get_debug_arch().set_dbgwcr(0, 0);
        sched.get_slot(2).state.capture_from_cpu(*sched.get_slot(2).cpu);
        TEST_CHECK(sched.compute_canonical_multicore_hash() == h1,
                   "Cluster hash returns when debug state is restored");
    }

    TEST_PASS("Suite 9: SMP debug isolation verified");
}

// =============================================================================
// Suite 10: DebugSnapshotReplayTests
// =============================================================================
void run_suite_10_debug_snapshot_replay() {
    std::cout << "Running Suite 10: DebugSnapshotReplayTests...\n";

    // SmpCoreState round-trip including the trailing debug block.
    {
        SmpCoreState src;
        src.reset(0, CoreLifecycleState::Runnable);
        src.debug.mdscr_el1 = (1ULL << 15u) | 1ULL;
        src.debug.mdccint_el1 = 0xA;
        src.debug.breakpoint_value[0] = 0x40105000ULL;
        src.debug.breakpoint_control[0] = bp_control(true, 1);
        src.debug.breakpoint_value[3] = 0x9000ULL;
        src.debug.breakpoint_control[3] = bp_control(true, 3) | (1u << 13u); // HMC stored
        src.debug.watchpoint_value[1] = 0x50000000ULL;
        src.debug.watchpoint_control[1] = wp_control(true, 3, 2);
        src.debug.watchpoint_control[2] = wp_control(true, 3, 3) | (1u << 24u);

        const auto buf = serialize_core_architectural_state(src);
        TEST_CHECK(buf.size() == kCoreArchPayloadSize, "Core payload includes the PMU block");

        SmpCoreState dst;
        const auto res = deserialize_core_architectural_state(buf.data(), buf.size(), dst);
        TEST_CHECK(res.success, "Debug core state deserializes");
        TEST_CHECK(src == dst, "Debug core state round-trips exactly");
        TEST_CHECK(src.compute_canonical_hash() == dst.compute_canonical_hash(),
                   "Round-trip preserves canonical hash");
        TEST_CHECK(dst.debug.breakpoint_control[3] == src.debug.breakpoint_control[3],
                   "Reserved/unsupported control bits round-trip verbatim");
    }

    // Legacy 1080-byte payload remains readable and resets debug state.
    {
        SmpCoreState src;
        src.reset(1, CoreLifecycleState::Runnable);
        src.debug.mdscr_el1 = 0x8001;
        const auto full = serialize_core_architectural_state(src);
        SmpCoreState legacy;
        legacy.debug.mdscr_el1 = 0xDEAD; // must be cleared when block absent
        const auto res = deserialize_core_architectural_state(
            full.data(), kCoreArchLegacyPayloadSize, legacy);
        TEST_CHECK(res.success, "Legacy 1080-byte payload is accepted");
        TEST_CHECK(legacy.debug.mdscr_el1 == 0 && legacy.debug.mdccint_el1 == 0,
                   "Legacy payload resets debug state");
    }

    // WSnap DebugState section (144 bytes) round-trip with MDCCINT.
    {
        DebugArchitecture src;
        src.set_mdscr_el1((1ULL << 15u) | 1ULL);
        src.set_mdccint_el1(0x9);
        src.set_dbgbvr(0, 0x40105000ULL);
        src.set_dbgbcr(0, 0x1u);
        src.set_dbgwvr(0, 0x50000000ULL);
        src.set_dbgwcr(0, 0x5u);

        const auto payload = snapshot::serialize_debug_state(src, 3);
        TEST_CHECK(payload.size() == 144, "WSnap debug section stays 144 bytes");

        DebugArchitecture dst;
        const auto res = snapshot::deserialize_debug_state(payload.data(), payload.size(), dst);
        TEST_CHECK(res.success, "WSnap debug section deserializes");
        TEST_CHECK(dst.get_mdscr_el1() == src.get_mdscr_el1(), "MDSCR restored");
        TEST_CHECK(dst.get_mdccint_el1() == 0x9, "MDCCINT restored from former pad");
        TEST_CHECK(dst.get_dbgbvr(0) == 0x40105000ULL, "DBGBVR restored");
        TEST_CHECK(dst.get_dbgwcr(0) == 0x5u, "DBGWCR restored");
    }

    // Pre-Stage-39 WSnap layout: zero at offset 4 decodes as MDCCINT=0.
    {
        std::vector<std::uint8_t> legacy(144, 0u);
        trace::encode_u64_le(legacy.data() + 8, 1ULL << 15u);
        DebugArchitecture dst;
        dst.set_mdccint_el1(0xF);
        const auto res = snapshot::deserialize_debug_state(legacy.data(), legacy.size(), dst);
        TEST_CHECK(res.success && dst.get_mdccint_el1() == 0,
                   "Legacy WSnap debug section clears MDCCINT");
        TEST_CHECK(dst.get_mdscr_el1() == (1ULL << 15u), "Legacy MDSCR restored");
    }

    // Full SMP snapshot/restore preserves per-core debug configuration.
    {
        auto shared_mem = std::make_shared<SmpSharedMemory>(0x40000);
        for (std::uint64_t addr = 0; addr < 0x40000; addr += 4) {
            shared_mem->raw_memory().write_u32(addr, kNop);
        }
        SmpScheduler sched(shared_mem, 4, 100);
        for (std::size_t i = 0; i < 4; ++i) {
            auto cpu = std::make_shared<CPU>();
            cpu->reset();
            cpu->set_cpacr_el1(0x00300000ULL);
            sched.register_core(i, cpu, CoreLifecycleState::Runnable);
            cpu->get_debug_arch().set_mdscr_el1(1ULL << 15u);
            cpu->get_debug_arch().set_mdccint_el1(static_cast<std::uint64_t>(i + 1));
            cpu->get_debug_arch().set_dbgbvr(0, 0x3000 + i * 0x10);
            cpu->get_debug_arch().set_dbgbcr(0, bp_control(true, 3));
            cpu->get_debug_arch().set_dbgwvr(0, 0x5000 + i * 0x10);
            cpu->get_debug_arch().set_dbgwcr(0, wp_control(true, 3, 3));
            sched.get_slot(i).state.capture_from_cpu(*cpu);
        }
        const auto pre_hash = sched.compute_canonical_multicore_hash();

        std::vector<std::uint8_t> snap;
        SmpSnapshotCaptureOptions opt{};
        opt.creator = "Stage39Test";
        TEST_CHECK(SmpSnapshotManager::capture_to_buffer(sched, snap, opt).success,
                   "SMP snapshot capture succeeds");

        for (std::size_t i = 0; i < 4; ++i) {
            sched.get_slot(i).cpu->get_debug_arch().reset();
            sched.get_slot(i).state.capture_from_cpu(*sched.get_slot(i).cpu);
        }
        TEST_CHECK(SmpSnapshotManager::restore_from_buffer(snap.data(), snap.size(), sched).success,
                   "SMP snapshot restore succeeds");

        for (std::size_t i = 0; i < 4; ++i) {
            const auto& d = sched.get_slot(i).cpu->get_debug_arch();
            TEST_CHECK(d.get_mdscr_el1() == (1ULL << 15u),
                       "Per-core MDSCR restored");
            TEST_CHECK(d.get_mdccint_el1() == i + 1,
                       "Per-core MDCCINT restored");
            TEST_CHECK(d.get_dbgbvr(0) == 0x3000 + i * 0x10,
                       "Per-core breakpoint address restored");
            TEST_CHECK(d.get_dbgwvr(0) == 0x5000 + i * 0x10,
                       "Per-core watchpoint address restored");
        }
        TEST_CHECK(sched.compute_canonical_multicore_hash() == pre_hash,
                   "Restored cluster hash matches pre-snapshot hash");
    }

    // Replay determinism: two identical configurations produce identical hashes.
    {
        auto make_cluster = [](std::uint64_t& out_hash) {
            auto shared_mem = std::make_shared<SmpSharedMemory>(0x10000);
            for (std::uint64_t a = 0; a < 0x10000; a += 4) {
                shared_mem->raw_memory().write_u32(a, kNop);
            }
            SmpScheduler sched(shared_mem, 2, 50);
            for (std::size_t i = 0; i < 2; ++i) {
                auto cpu = std::make_shared<CPU>();
                cpu->reset();
                cpu->set_cpacr_el1(0x00300000ULL);
                cpu->set_program_counter(0x1000 + i * 0x100);
                cpu->get_debug_arch().set_mdscr_el1(1ULL << 15u);
                cpu->get_debug_arch().set_dbgbvr(0, 0x5000 + i * 0x100);
                cpu->get_debug_arch().set_dbgbcr(0, bp_control(true, 3));
                sched.register_core(i, cpu, CoreLifecycleState::Runnable);
            }
            for (int q = 0; q < 20; ++q) {
                sched.step_quantum();
            }
            out_hash = sched.compute_canonical_multicore_hash();
        };
        std::uint64_t h1 = 0, h2 = 0;
        make_cluster(h1);
        make_cluster(h2);
        TEST_CHECK(h1 != 0 && h1 == h2, "Deterministic replay hash with debug state");
    }

    TEST_PASS("Suite 10: debug snapshot and replay verified");
}

// =============================================================================
// Suite 11: DebugReferenceDifferentialTests
// =============================================================================
void run_suite_11_debug_reference_differential() {
    std::cout << "Running Suite 11: DebugReferenceDifferentialTests...\n";

    Lfsr rng(0x39DEB001u);
    std::size_t compared = 0;

    // Control classification parity over random encodings.
    for (int i = 0; i < 4000; ++i) {
        const std::uint32_t control = rng.next();
        const auto prod = DebugArchitecture::decode_breakpoint_control(control);
        const auto ref = ReferenceDebugModel::decode_breakpoint(control);
        TEST_CHECK(prod.enabled == ref.enabled, "BP enabled classification parity");
        TEST_CHECK(prod.hmc == ref.hmc, "BP HMC classification parity");
        TEST_CHECK(prod.ssc == ref.ssc, "BP SSC classification parity");
        TEST_CHECK(prod.type == ref.type, "BP BT parity");
        TEST_CHECK(prod.active == ref.active, "BP active classification parity");

        const auto wprod = DebugArchitecture::decode_watchpoint_control(control);
        const auto wref = ReferenceDebugModel::decode_watchpoint(control);
        TEST_CHECK(wprod.enabled == wref.enabled, "WP enabled parity");
        TEST_CHECK(wprod.mask == wref.mask, "WP MASK parity");
        TEST_CHECK(wprod.lsc == static_cast<WatchpointLoadStoreControl>(wref.lsc),
                   "WP LSC parity");
        TEST_CHECK(wprod.active == wref.active, "WP active parity");
        ++compared;
    }

    // Matching parity over random register files and accesses.
    std::array<std::uint64_t, 4> bv{};
    std::array<std::uint32_t, 4> bc{};
    std::array<std::uint64_t, 4> wv{};
    std::array<std::uint32_t, 4> wc{};

    for (int iter = 0; iter < 3000; ++iter) {
        for (std::size_t s = 0; s < 4; ++s) {
            const std::uint32_t raw_b = rng.next();
            // Bias towards meaningful addresses/controls but keep reserved bits.
            bv[s] = static_cast<std::uint64_t>(rng.next() & 0x7FFFu) * 4u;
            bc[s] = (rng.next() & 1u) ? bp_control(true, raw_b & 3u,
                                                   (raw_b >> 4) & 3u, raw_b & 3u,
                                                   (raw_b >> 8) & 0xFu)
                                      : 0u;
            if ((raw_b & 0x80u) != 0u) bc[s] |= (1u << 13u); // sometimes HMC
            if ((raw_b & 0x100u) != 0u) bc[s] |= ((raw_b >> 1) & 3u) << 14u; // sometimes SSC

            const std::uint32_t raw_w = rng.next();
            wv[s] = static_cast<std::uint64_t>(rng.next() & 0x7FFFu) * 4u;
            wc[s] = (rng.next() & 1u) ? wp_control(true, raw_w & 3u, 1u + ((raw_w >> 2) & 3u),
                                                   (raw_w >> 4) & 0xFFu, raw_w & 3u,
                                                   (raw_w & 0x200u) != 0u)
                                      : 0u;
            if ((raw_w & 0x400u) != 0u) wc[s] |= (1u << 24u); // sometimes MASK
        }

        DebugArchitecture prod;
        prod.set_mdscr_el1(1ULL << 15u);
        for (std::size_t s = 0; s < 4; ++s) {
            prod.set_dbgbvr(s, bv[s]);
            prod.set_dbgbcr(s, bc[s]);
            prod.set_dbgwvr(s, wv[s]);
            prod.set_dbgwcr(s, wc[s]);
        }

        const unsigned el = rng.next() % 4u;
        const std::uint64_t pc = static_cast<std::uint64_t>(rng.next() & 0x7FFFu) * 4u;
        const std::uint64_t addr = static_cast<std::uint64_t>(rng.next() & 0x7FFFu) * 4u;
        const std::size_t size = 1u + (rng.next() % 16u);
        const bool write = (rng.next() & 1u) != 0u;

        const auto prod_bp = prod.check_execution_breakpoint(pc, el);
        const int ref_bp = ReferenceDebugModel::match_breakpoint(
            bv.data(), bc.data(), pc, el);
        const int prod_bp_slot = prod_bp.has_value()
                                     ? static_cast<int>(prod_bp->slot_index)
                                     : -1;
        TEST_CHECK(prod_bp_slot == ref_bp, "Breakpoint match differential parity");

        const auto prod_wp = prod.check_data_watchpoint(
            addr, size, write ? DebugAccessType::Write : DebugAccessType::Read, el, pc);
        const int ref_wp = ReferenceDebugModel::match_watchpoint(
            wv.data(), wc.data(), bv.data(), bc.data(), addr, size, write, el, pc);
        const int prod_wp_slot = prod_wp.has_value()
                                     ? static_cast<int>(prod_wp->slot_index)
                                     : -1;
        TEST_CHECK(prod_wp_slot == ref_wp, "Watchpoint match differential parity");
        ++compared;
    }

    // Syndrome differential (production constructor vs reference formulas).
    for (unsigned same = 0; same <= 1; ++same) {
        DebugExceptionEvent bp;
        bp.kind = DebugExceptionKind::HardwareBreakpoint;
        bp.source_el = same ? 1u : 0u;
        bp.instruction_address = 0x4000;
        const auto bp_ex = create_debug_synchronous_exception(bp, 1);
        TEST_CHECK(bp_ex.esr() == ReferenceDebugModel::breakpoint_esr(same != 0),
                   "Breakpoint syndrome differential parity");

        for (unsigned wr = 0; wr <= 1; ++wr) {
            DebugExceptionEvent wp;
            wp.kind = DebugExceptionKind::HardwareWatchpoint;
            wp.source_el = same ? 1u : 0u;
            wp.fault_address = 0x5000;
            wp.is_write = wr != 0;
            const auto wp_ex = create_debug_synchronous_exception(wp, 1);
            TEST_CHECK(wp_ex.esr() == ReferenceDebugModel::watchpoint_esr(same != 0, wr != 0),
                       "Watchpoint syndrome differential parity");

            DebugExceptionEvent ss;
            ss.kind = DebugExceptionKind::SoftwareStep;
            ss.source_el = same ? 1u : 0u;
            ss.is_exclusive = wr != 0;
            const auto ss_ex = create_debug_synchronous_exception(ss, 1);
            TEST_CHECK(ss_ex.esr() == ReferenceDebugModel::software_step_esr(same != 0, wr != 0),
                       "Software-step syndrome differential parity");
        }
    }

    // Explicit third-party table (not derived from either implementation).
    struct TableRow { std::uint32_t ec; std::uint32_t esr; };
    const TableRow table[] = {
        {0x30u, 0xC0000000u}, {0x31u, 0xC4000000u},
        {0x32u, 0xC8000000u}, {0x33u, 0xCC000000u},
        {0x34u, 0xD0000000u}, {0x35u, 0xD4000000u},
    };
    for (const auto& row : table) {
        if (row.ec == 0x30u || row.ec == 0x31u) {
            TEST_CHECK((ReferenceDebugModel::breakpoint_esr(row.ec == 0x31u) & 0xFC000000u) ==
                           (row.esr & 0xFC000000u),
                       "Table row sanity");
        }
    }
    TEST_CHECK(ReferenceDebugModel::breakpoint_esr(false) == 0xC2000000u, "BP lower table value");
    TEST_CHECK(ReferenceDebugModel::breakpoint_esr(true) == 0xC6000000u, "BP same table value");
    TEST_CHECK(ReferenceDebugModel::software_step_esr(false, false) == 0xCA000022u,
               "SS lower table value");
    TEST_CHECK(ReferenceDebugModel::software_step_esr(true, false) == 0xCE000022u,
               "SS same table value");
    TEST_CHECK(ReferenceDebugModel::watchpoint_esr(false, true) == 0xD2000062u,
               "WP lower write table value");
    TEST_CHECK(ReferenceDebugModel::watchpoint_esr(true, false) == 0xD6000022u,
               "WP same read table value");

    TEST_CHECK(compared >= 7000, "Differential comparison volume is meaningful");
    TEST_PASS("Suite 11: reference differential parity verified");
}

// =============================================================================
// Suite 12: Stage39DebugFuzzTests
// =============================================================================
void run_suite_12_fuzz() {
    std::cout << "Running Suite 12: Stage39DebugFuzzTests...\n";

    Lfsr rng(0xF0223901u);

    // 1. Control-space fuzzing: invariants hold for arbitrary 32-bit values.
    for (int i = 0; i < 20000; ++i) {
        const std::uint32_t control = rng.next();

        const auto bp = DebugArchitecture::decode_breakpoint_control(control);
        if (!bp.enabled) {
            TEST_CHECK(!bp.active, "Disabled breakpoint encoding is never active");
        }
        if (bp.active) {
            TEST_CHECK(!bp.hmc && bp.ssc == 0 && (bp.type == 0 || bp.type == 1),
                       "Active breakpoint encoding has only modeled fields");
        }
        const auto bp2 = DebugArchitecture::decode_breakpoint_control(control);
        TEST_CHECK(std::memcmp(&bp, &bp2, sizeof(bp)) == 0,
                   "Breakpoint decode is deterministic");

        const auto wp = DebugArchitecture::decode_watchpoint_control(control);
        if (!wp.enabled) {
            TEST_CHECK(!wp.active, "Disabled watchpoint encoding is never active");
        }
        if (wp.active) {
            TEST_CHECK(!wp.hmc && wp.ssc == 0 && wp.mask == 0 &&
                       wp.lsc != WatchpointLoadStoreControl::Neither,
                       "Active watchpoint encoding has only modeled fields");
        }

        // Production/reference classification parity on raw encodings.
        const auto ref_bp = ReferenceDebugModel::decode_breakpoint(control);
        const auto ref_wp = ReferenceDebugModel::decode_watchpoint(control);
        TEST_CHECK(bp.active == ref_bp.active && wp.active == ref_wp.active,
                   "Fuzz classification parity with reference");
    }

    // 2. Matching fuzzing: unsupported slots can never fire.
    for (int iter = 0; iter < 5000; ++iter) {
        DebugArchitecture dbg;
        dbg.set_mdscr_el1(1ULL << 15u);
        std::array<std::uint64_t, 4> addresses{};
        for (std::size_t s = 0; s < 4; ++s) {
            addresses[s] = static_cast<std::uint64_t>(rng.next() & 0xFFFFu);
            std::uint32_t control = rng.next();
            if ((control & 1u) == 0u) {
                control |= 1u; // force enabled to exercise gating
            }
            // Force at least one unsupported field in half the cases.
            if ((rng.next() & 1u) != 0u) {
                control |= (1u << 13u); // HMC
            }
            dbg.set_dbgbvr(s, addresses[s]);
            dbg.set_dbgbcr(s, control);
            dbg.set_dbgwvr(s, addresses[s]);
            dbg.set_dbgwcr(s, control);
        }
        const std::size_t probe = rng.next() % 4u;
        const auto bp_ev = dbg.check_execution_breakpoint(addresses[probe], 1);
        if (bp_ev.has_value()) {
            TEST_CHECK(dbg.breakpoint_slot_active(bp_ev->slot_index),
                       "Only an active breakpoint slot can match");
        }
        const auto wp_ev = dbg.check_data_watchpoint(
            addresses[probe], 1,
            (rng.next() & 1u) ? DebugAccessType::Write : DebugAccessType::Read, 1);
        if (wp_ev.has_value()) {
            TEST_CHECK(dbg.watchpoint_slot_active(wp_ev->slot_index),
                       "Only an active watchpoint slot can match");
        }

        // Random access addresses must not match when no slot is active.
        if (dbg.active_breakpoint_count() == 0) {
            TEST_CHECK(!dbg.check_execution_breakpoint(rng.next() & 0xFFFFu, 1).has_value(),
                       "No active breakpoint can match");
        }
        if (dbg.active_watchpoint_count() == 0) {
            TEST_CHECK(!dbg.check_data_watchpoint(rng.next() & 0xFFFFu, 8,
                                                  DebugAccessType::Read, 1).has_value(),
                       "No active watchpoint can match");
        }
    }

    // 3. Snapshot deserialization fuzzing: truncated and corrupted buffers are
    //    handled without out-of-bounds access.
    for (int iter = 0; iter < 2000; ++iter) {
        std::vector<std::uint8_t> buffer(1400);
        for (auto& b : buffer) {
            b = static_cast<std::uint8_t>(rng.next() & 0xFFu);
        }
        const std::size_t len = rng.next() % (buffer.size() + 1);
        SmpCoreState state;
        const auto res = deserialize_core_architectural_state(buffer.data(), len, state);
        if (len < 1056) {
            TEST_CHECK(!res.success, "Sub-1056 buffers always fail");
        } else {
            TEST_CHECK(res.success, "At-least-1056 buffers always deserialize");
            // Re-serialization must be well-formed regardless of content.
            const auto again = serialize_core_architectural_state(state);
            TEST_CHECK(again.size() == kCoreArchPayloadSize,
                       "Fuzzed state re-serializes to the canonical size");
        }
    }

    // 4. WSnap debug-section fuzzing (fixed 144-byte size).
    for (int iter = 0; iter < 2000; ++iter) {
        std::vector<std::uint8_t> buffer(144);
        for (auto& b : buffer) {
            b = static_cast<std::uint8_t>(rng.next() & 0xFFu);
        }
        DebugArchitecture dbg;
        const auto res = snapshot::deserialize_debug_state(buffer.data(), buffer.size(), dbg);
        TEST_CHECK(res.success, "144-byte debug section always deserializes");
        TEST_CHECK((dbg.get_mdccint_el1() & ~0xFull) == 0,
                   "Fuzzed MDCCINT stays within modeled bits");
        TEST_CHECK((dbg.get_mdscr_el1() & ~0x8001ULL) == 0,
                   "Fuzzed MDSCR stays within modeled bits");
    }

    TEST_PASS("Suite 12: 29,000-iteration debug fuzzing completed");
}

// =============================================================================
// Suite 13: Stage39DebugEnduranceTests
// =============================================================================
void run_suite_13_endurance() {
    std::cout << "Running Suite 13: Stage39DebugEnduranceTests...\n";

    // 1. 250,000 breakpoint/watchpoint evaluations remain deterministic.
    {
        DebugArchitecture dbg;
        dbg.set_mdscr_el1(1ULL << 15u);
        for (std::size_t s = 0; s < 4; ++s) {
            dbg.set_dbgbvr(s, 0x4000 + s * 0x100);
            dbg.set_dbgbcr(s, bp_control(true, 3));
            dbg.set_dbgwvr(s, 0x8000 + s * 0x100);
            dbg.set_dbgwcr(s, wp_control(true, 3, 3));
        }
        std::uint64_t checks = 0;
        std::uint64_t breakpoints = 0;
        std::uint64_t watchpoints = 0;
        for (std::uint32_t i = 0; i < 250000u; ++i) {
            const std::uint64_t addr = (i & 1u) != 0u
                ? 0x8000 + ((i * 37u) % 0x400u)
                : 0x4000 + ((i * 37u) % 0x400u);
            if (dbg.check_execution_breakpoint(addr, 1).has_value()) {
                ++breakpoints;
            }
            if (dbg.check_data_watchpoint(addr, 4, DebugAccessType::Read, 1).has_value()) {
                ++watchpoints;
            }
            checks += 2;
        }
        TEST_CHECK(checks == 500000, "500,000 evaluations completed");
        TEST_CHECK(breakpoints > 0 && watchpoints > 0, "Endurance run exercised hits");
        TEST_CHECK(dbg.debug_event_sequence() == breakpoints + watchpoints,
                   "Event sequence counts exactly the recorded hits");
    }

    // 2. 5,000 snapshot capture/restore/hash cycles.
    {
        SmpCoreState state;
        state.reset(2, CoreLifecycleState::Runnable);
        std::uint64_t expected = 0;
        for (int i = 0; i < 5000; ++i) {
            state.debug.mdscr_el1 = static_cast<std::uint64_t>(i & 0x8001);
            state.debug.mdccint_el1 = static_cast<std::uint64_t>(i & 0xF);
            state.debug.breakpoint_value[0] = static_cast<std::uint64_t>(i) * 0x10;
            state.debug.breakpoint_control[0] =
                bp_control((i & 1) != 0, static_cast<unsigned>(i & 3));
            state.debug.watchpoint_control[1] =
                wp_control((i & 2) != 0, static_cast<unsigned>(i & 3), 3);

            const auto buf = serialize_core_architectural_state(state);
            SmpCoreState restored;
            TEST_CHECK(deserialize_core_architectural_state(buf.data(), buf.size(), restored).success,
                       "Endurance snapshot deserializes");
            TEST_CHECK(restored == state, "Endurance snapshot round-trips");
            const auto h = restored.compute_canonical_hash();
            if (i > 0) {
                TEST_CHECK(h != expected, "Canonical hash changes with debug configuration");
            }
            expected = h;
        }
    }

    // 3. 8-core deterministic endurance with per-core breakpoints.
    {
        auto run = [](std::uint64_t& hash_out) {
            auto shared_mem = std::make_shared<SmpSharedMemory>(0x40000);
            for (std::uint64_t a = 0; a < 0x40000; a += 4) {
                shared_mem->raw_memory().write_u32(a, kNop);
            }
            for (std::uint64_t v = 0; v < 0x800; v += 0x80) {
                shared_mem->raw_memory().write_u32(v, 0xD69F03E0u); // ERET
            }
            SmpScheduler sched(shared_mem, 8, 20);
            for (std::size_t i = 0; i < 8; ++i) {
                auto cpu = std::make_shared<CPU>();
                cpu->reset();
                cpu->set_cpacr_el1(0x00300000ULL);
                cpu->set_program_counter(0x2000 + i * 0x200);
                cpu->get_debug_arch().set_mdscr_el1(1ULL << 15u);
                cpu->get_debug_arch().set_dbgbvr(0, 0x3000 + i * 0x200);
                cpu->get_debug_arch().set_dbgbcr(0, bp_control(true, 3));
                cpu->get_debug_arch().set_dbgwvr(0, 0x7000 + i * 0x200);
                cpu->get_debug_arch().set_dbgwcr(0, wp_control(true, 3, 3));
                sched.register_core(i, cpu, CoreLifecycleState::Runnable);
            }
            for (int q = 0; q < 400; ++q) {
                sched.step_quantum();
            }
            hash_out = sched.compute_canonical_multicore_hash();
        };
        std::uint64_t h1 = 0, h2 = 0;
        run(h1);
        run(h2);
        TEST_CHECK(h1 != 0 && h1 == h2,
                   "8-core debug endurance run is deterministic");
    }

    // 4. Reset returns every modeled debug field to its architectural value.
    {
        DebugArchitecture dbg;
        dbg.set_mdscr_el1(0xFFFF);
        dbg.set_mdccint_el1(0xF);
        dbg.set_dbgbvr(0, 0x1234);
        dbg.set_dbgbcr(0, 0x1);
        dbg.set_dbgwvr(0, 0x5678);
        dbg.set_dbgwcr(0, wp_control(true, 3, 3));
        dbg.record_debug_event(DebugEvent{DebugEventType::Breakpoint, 0x1000});
        dbg.reset();
        TEST_CHECK(dbg.get_mdscr_el1() == 0 && dbg.get_mdccint_el1() == 0 &&
                   dbg.get_dbgbvr(0) == 0 && dbg.get_dbgbcr(0) == 0 &&
                   dbg.get_dbgwvr(0) == 0 && dbg.get_dbgwcr(0) == 0,
                   "Reset clears all modeled debug state");
        TEST_CHECK(dbg.pending_event() == nullptr && dbg.debug_event_sequence() == 0,
                   "Reset clears event accounting");
    }

    TEST_PASS("Suite 13: debug endurance verified");
}

// =============================================================================
// Main Test Dispatcher
// =============================================================================
int main(int argc, char* argv[]) {
    std::cout << "========================================================\n";
    std::cout << "WinIOS Phase 52 Stage 39: Architectural Debug Tests\n";
    std::cout << "========================================================\n";

    int suite_to_run = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--suite") == 0 && i + 1 < argc) {
            suite_to_run = std::atoi(argv[++i]);
        }
    }

    try {
        if (suite_to_run == 0 || suite_to_run == 1)  run_suite_1_debug_register_architecture();
        if (suite_to_run == 0 || suite_to_run == 2)  run_suite_2_hardware_breakpoint_matching();
        if (suite_to_run == 0 || suite_to_run == 3)  run_suite_3_hardware_watchpoint_matching();
        if (suite_to_run == 0 || suite_to_run == 4)  run_suite_4_debug_privilege_enforcement();
        if (suite_to_run == 0 || suite_to_run == 5)  run_suite_5_debug_exception_syndrome();
        if (suite_to_run == 0 || suite_to_run == 6)  run_suite_6_debug_vector_selection();
        if (suite_to_run == 0 || suite_to_run == 7)  run_suite_7_single_step_execution();
        if (suite_to_run == 0 || suite_to_run == 8)  run_suite_8_debug_mmu_interaction();
        if (suite_to_run == 0 || suite_to_run == 9)  run_suite_9_debug_smp_isolation();
        if (suite_to_run == 0 || suite_to_run == 10) run_suite_10_debug_snapshot_replay();
        if (suite_to_run == 0 || suite_to_run == 11) run_suite_11_debug_reference_differential();
        if (suite_to_run == 0 || suite_to_run == 12) run_suite_12_fuzz();
        if (suite_to_run == 0 || suite_to_run == 13) run_suite_13_endurance();
    } catch (const std::exception& ex) {
        std::cerr << "UNHANDLED EXCEPTION: " << ex.what() << "\n";
        g_failed_tests++;
    }

    std::cout << "========================================================\n";
    if (g_failed_tests == 0) {
        std::cout << "ALL STAGE 39 TESTS PASSED SUCCESSFULLY!\n";
        return 0;
    }
    std::cerr << "STAGE 39 TESTS FAILED: " << g_failed_tests << " failure(s)\n";
    return 1;
}
