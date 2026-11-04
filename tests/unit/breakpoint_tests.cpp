#include "cpu/cpu.h"
#include "cpu/smp_cluster.h"
#include "debugger/debugger.h"
#include "memory/memory.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// WinIOS Phase 51 Stage 4: AArch64 Hardware Execution Breakpoint Integration Tests
// Validates actual hardware execution breakpoint triggering across 4 slots,
// privilege-mode filtering, instruction suppression, exception routing (EL1, EL2, EL3),
// precedence rules, ERET handling, SMP multi-core isolation, and software debugger coexistence.

namespace {

static std::size_t g_checks_passed = 0;
static std::size_t g_checks_failed = 0;

#define CHECK(cond) do { \
    if (cond) { \
        ++g_checks_passed; \
    } else { \
        ++g_checks_failed; \
        std::cerr << "FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    if ((a) == (b)) { \
        ++g_checks_passed; \
    } else { \
        ++g_checks_failed; \
        std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
    } \
} while (0)

// Helper: encode MOVZ Xd, #imm16, LSL #shift
constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm16, unsigned shift = 0) {
    return 0xD2800000u | ((shift / 16u) << 21u) | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}

// Helper: encode ADD Xd, Xn, #imm12
constexpr std::uint32_t enc_add_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0x91000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

// Helper: encode ERET (0xD69F03E0)
constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}

// Helper: encode MSR DBGBCR<n>_EL1, Xt
// DBGBCR<n>_EL1: op0=2, op1=0, CRn=0, CRm=n, op2=5
constexpr std::uint32_t enc_msr_dbgbcr(unsigned slot, unsigned rt) {
    const unsigned o0 = 0; // op0=2 -> o0=0
    const unsigned op1 = 0;
    const unsigned crn = 0;
    const unsigned crm = slot & 0xFu;
    const unsigned op2 = 5;
    return 0xD5100000u | (o0 << 19u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

// Setup standard CPU with memory and vector table at 0x10000
void setup_cpu(CPU& cpu, Memory& mem, unsigned el = 1, std::uint64_t vbar = 0x10000) {
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(el);
    cpu.set_vbar_el1(vbar);
    cpu.set_vbar_el2(vbar + 0x10000);
    cpu.set_vbar_el3(vbar + 0x20000);
    // Fill memory with NOPs (0xD503201F)
    for (std::size_t i = 0; i < mem.size(); i += 4) {
        mem.write_u32(i, 0xD503201Fu);
    }
}

// =========================================================================
// Tests 1-4: Execution Breakpoint Hit on Slots 0..3
// =========================================================================

void test_1_slot0_breakpoint_hit() {
    std::cout << "Test 1: Slot 0 execution breakpoint hit\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 42)); // MOVZ X0, #42

    // Enable MDE in MDSCR_EL1
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    // Slot 0: DBGBVR0 = 0x2000, DBGBCR0 = E(1) | PMC(0b11 << 1)
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    // PC should have jumped to VBAR_EL1 + 0x200 (same EL vector)
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_elr_el1(), 0x2000ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x31u); // EC 0x31 = BreakpointSame
    CHECK_EQ(cpu.read_register(0), 0ULL);      // Instruction was suppressed!
}

void test_2_slot1_breakpoint_hit() {
    std::cout << "Test 2: Slot 1 execution breakpoint hit\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(1, 100));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    // Slot 1: DBGBVR1 = 0x2000, DBGBCR1 = E(1) | PMC(0b11 << 1)
    cpu.get_debug_arch().set_dbgbvr(1, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(1, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_elr_el1(), 0x2000ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x31u);
    CHECK_EQ(cpu.read_register(1), 0ULL);
}

void test_3_slot2_breakpoint_hit() {
    std::cout << "Test 3: Slot 2 execution breakpoint hit\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x3000);
    mem.write_u32(0x3000, enc_movz(2, 200));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(2, 0x3000);
    cpu.get_debug_arch().set_dbgbcr(2, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_elr_el1(), 0x3000ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x31u);
    CHECK_EQ(cpu.read_register(2), 0ULL);
}

void test_4_slot3_breakpoint_hit() {
    std::cout << "Test 4: Slot 3 execution breakpoint hit\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x4000);
    mem.write_u32(0x4000, enc_movz(3, 300));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(3, 0x4000);
    cpu.get_debug_arch().set_dbgbcr(3, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_elr_el1(), 0x4000ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x31u);
    CHECK_EQ(cpu.read_register(3), 0ULL);
}

// =========================================================================
// Tests 5-7: Disabled Breakpoints and Address Mismatch
// =========================================================================

void test_5_disabled_breakpoint_does_not_trigger() {
    std::cout << "Test 5: Disabled breakpoint (E=0) does not trigger\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 42));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x0u); // E = 0 (disabled)

    cpu.step(mem);

    // Instruction should execute normally
    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    CHECK_EQ(cpu.read_register(0), 42ULL);
}

void test_6_mde_zero_disables_all_breakpoints() {
    std::cout << "Test 6: MDE=0 disables all breakpoints globally\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 99));

    // MDE = 0 (disabled)
    cpu.get_debug_arch().set_mdscr_el1(0ULL);
    // Breakpoint enabled
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    CHECK_EQ(cpu.read_register(0), 99ULL);
}

void test_7_non_matching_address_does_not_trigger() {
    std::cout << "Test 7: Breakpoint with non-matching address does not trigger\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 77));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    // Breakpoint set to 0x2008, not 0x2000
    cpu.get_debug_arch().set_dbgbvr(0, 0x2008);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    CHECK_EQ(cpu.read_register(0), 77ULL);
}

// =========================================================================
// Tests 8-9: Precedence Rules
// =========================================================================

void test_8_unaligned_pc_precedence_over_breakpoint() {
    std::cout << "Test 8: Unaligned PC fault takes precedence over breakpoint\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Unaligned PC: 0x2001
    cpu.set_program_counter(0x2001);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2001);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    // PC alignment fault must be raised: EC = 0x22 (PcAlignment), not 0x31
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_elr_el1(), 0x2001ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x22u); // EC 0x22 = PcAlignment
}

void test_9_breakpoint_precedence_over_mmu_translation_fault() {
    std::cout << "Test 9: Breakpoint takes precedence over MMU translation fault\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Set PC to an unmapped virtual address that would cause an abort if fetched
    // Here we can configure a breakpoint at 0x80000000 (outside physical RAM limit)
    // When MMU is off or translation fails, breakpoint fires BEFORE MMU fault!
    cpu.set_program_counter(0x80000000ULL);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x80000000ULL);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    // Breakpoint exception must be taken, NOT an Instruction Abort (0x21)
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_elr_el1(), 0x80000000ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x31u); // EC 0x31 = BreakpointSame
}

// =========================================================================
// Tests 10-12: Instruction Suppression and ELR Behavior
// =========================================================================

void test_10_instruction_suppression_registers_unchanged() {
    std::cout << "Test 10: Breakpoint suppresses instruction execution (registers unchanged)\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(0, 0, 50)); // ADD X0, X0, #50
    cpu.write_register(0, 12345ULL);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    // Register X0 must remain strictly unchanged
    CHECK_EQ(cpu.read_register(0), 12345ULL);
}

void test_11_instruction_suppression_pc_does_not_advance_past() {
    std::cout << "Test 11: Breakpoint suppresses instruction execution (PC does not advance past)\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(5, 500));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    // PC jumped to exception vector, NOT 0x2004
    CHECK(cpu.get_program_counter() != 0x2004ULL);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
}

void test_12_elr_el1_set_to_instruction_address() {
    std::cout << "Test 12: ELR_EL1 set to breakpoint instruction address\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    const std::uint64_t bp_addr = 0x543CULL;
    cpu.set_program_counter(bp_addr);
    mem.write_u32(static_cast<std::size_t>(bp_addr), enc_movz(0, 1));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, bp_addr);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    CHECK_EQ(cpu.get_elr_el1(), bp_addr);
}

// =========================================================================
// Tests 13-15: ESR EC, Lower vs Same EL, and SPSR Save
// =========================================================================

void test_13_esr_ec_lower_el() {
    std::cout << "Test 13: ESR_EL1 reports EC=0x30 for lower-EL (EL0) breakpoint\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 0, 0x10000); // Start at EL0

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 1));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    // Breakpoint matching EL0 (PMC = 0b10 or 0b11)
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x2u << 1u)); // PMC = 0b10 (EL0)

    cpu.step(mem);

    // Vector for lower EL: VBAR_EL1 + 0x400
    CHECK_EQ(cpu.get_program_counter(), 0x10400ULL);
    CHECK_EQ(cpu.get_current_el(), 1u);
    CHECK_EQ(cpu.get_elr_el1(), 0x2000ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x30u); // EC 0x30 = BreakpointLower
    CHECK((cpu.get_esr_el1() & (1u << 25u)) != 0u); // IL bit = 1
    CHECK_EQ(cpu.get_esr_el1() & 0x01FFFFFFu, 0u);   // ISS = 0
}

void test_14_esr_ec_same_el() {
    std::cout << "Test 14: ESR_EL1 reports EC=0x31 for same-EL (EL1) breakpoint\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000); // Start at EL1

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 1));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x1u << 1u)); // PMC = 0b01 (EL1)

    cpu.step(mem);

    // Vector for same EL: VBAR_EL1 + 0x200
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_current_el(), 1u);
    CHECK_EQ(cpu.get_elr_el1(), 0x2000ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x31u); // EC 0x31 = BreakpointSame
    CHECK((cpu.get_esr_el1() & (1u << 25u)) != 0u);
    CHECK_EQ(cpu.get_esr_el1() & 0x01FFFFFFu, 0u);
}

void test_15_spsr_el1_saves_pre_exception_pstate() {
    std::cout << "Test 15: SPSR_EL1 saves pre-exception PSTATE\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 1));
    cpu.set_nzcv(0xAu); // N=1, Z=0, C=1, V=0 -> 0xA in high 4 bits
    cpu.set_daif(0x2u); // IRQ unmasked, FIQ masked

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    const std::uint32_t spsr = cpu.get_spsr_el1();
    // Verify NZCV preserved in SPSR[31:28]
    CHECK_EQ((spsr >> 28u) & 0xFu, 0xAu);
    // Verify DAIF saved in SPSR[9:6]
    CHECK_EQ((spsr >> 6u) & 0xFu, 0x2u);
    // Verify Exception Level EL1 saved in SPSR[3:0] (EL1h = 0b0101)
    CHECK_EQ(spsr & 0xFu, 0x5u);
}

// =========================================================================
// Tests 16-19: Privilege Filtering (PMC Modes)
// =========================================================================

void test_16_privilege_filtering_pmc_00_matches_all_els() {
    std::cout << "Test 16: Privilege filtering: PMC=0b00 matches all ELs\n";
    // Test at EL0
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 0, 0x10000);
        cpu.set_program_counter(0x2000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x0u << 1u)); // PMC = 0b00
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x10400ULL);
    }
    // Test at EL1
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x0u << 1u)); // PMC = 0b00
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    }
}

void test_17_privilege_filtering_pmc_01_matches_only_el1() {
    std::cout << "Test 17: Privilege filtering: PMC=0b01 matches only EL1\n";
    // At EL0: should NOT match
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 0, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_movz(0, 10));
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x1u << 1u)); // PMC = 0b01 (EL1 only)
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
        CHECK_EQ(cpu.read_register(0), 10ULL);
    }
    // At EL1: SHOULD match
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_movz(0, 10));
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x1u << 1u)); // PMC = 0b01 (EL1 only)
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
        CHECK_EQ(cpu.read_register(0), 0ULL);
    }
}

void test_18_privilege_filtering_pmc_10_matches_only_el0() {
    std::cout << "Test 18: Privilege filtering: PMC=0b10 matches only EL0\n";
    // At EL1: should NOT match
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_movz(0, 20));
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x2u << 1u)); // PMC = 0b10 (EL0 only)
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
        CHECK_EQ(cpu.read_register(0), 20ULL);
    }
    // At EL0: SHOULD match
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 0, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_movz(0, 20));
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x2u << 1u)); // PMC = 0b10 (EL0 only)
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x10400ULL);
        CHECK_EQ(cpu.read_register(0), 0ULL);
    }
}

void test_19_privilege_filtering_pmc_11_matches_el0_and_el1() {
    std::cout << "Test 19: Privilege filtering: PMC=0b11 matches EL0 and EL1\n";
    // At EL0: matches
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 0, 0x10000);
        cpu.set_program_counter(0x2000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x10400ULL);
    }
    // At EL1: matches
    {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
        cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));
        cpu.step(mem);
        CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    }
}

// =========================================================================
// Test 20: Slot Priority (Lowest numbered slot matches first)
// =========================================================================

void test_20_lowest_numbered_slot_matches_first() {
    std::cout << "Test 20: Lowest-numbered slot matches first\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);

    // Enable both Slot 1 and Slot 2 for address 0x2000
    cpu.get_debug_arch().set_dbgbvr(1, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(1, 0x1u | (0x3u << 1u));
    cpu.get_debug_arch().set_dbgbvr(2, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(2, 0x1u | (0x3u << 1u));

    // Verify through DebugArchitecture check that slot 1 fires first
    const auto ev = cpu.get_debug_arch().check_execution_breakpoint(0x2000, 1);
    CHECK(ev.has_value());
    CHECK_EQ(ev->slot_index, 1u);

    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
}

// =========================================================================
// Tests 21-22: ERET Re-execution and Breakpoint Disable
// =========================================================================

void test_21_eret_reexecutes_if_breakpoint_not_disabled() {
    std::cout << "Test 21: ERET from breakpoint handler re-executes if breakpoint not disabled\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 42));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    // Handler at VBAR_EL1 + 0x200 (0x10200) contains just ERET
    mem.write_u32(0x10200, enc_eret());

    // Step 1: hits breakpoint at 0x2000, jumps to 0x10200
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);

    // Step 2: executes ERET at 0x10200, restores PC to 0x2000
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x2000ULL);

    // Step 3: hits breakpoint at 0x2000 AGAIN!
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.read_register(0), 0ULL); // Instruction at 0x2000 never executed
}

void test_22_breakpoint_disable_allows_instruction_to_execute() {
    std::cout << "Test 22: Breakpoint followed by disable allows instruction to execute\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 42));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    // Handler at 0x10200 disables breakpoint 0 and does ERET:
    // MOVZ X1, #0
    // MSR DBGBCR0_EL1, X1
    // ERET
    mem.write_u32(0x10200, enc_movz(1, 0));
    mem.write_u32(0x10204, enc_msr_dbgbcr(0, 1));
    mem.write_u32(0x10208, enc_eret());

    // Step 1: Hit breakpoint at 0x2000 -> vector 0x10200
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);

    // Step 2: MOVZ X1, #0
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10204ULL);

    // Step 3: MSR DBGBCR0_EL1, X1 (disables breakpoint 0)
    cpu.step(mem);
    CHECK_EQ(cpu.get_debug_arch().get_dbgbcr(0), 0u);
    CHECK_EQ(cpu.get_program_counter(), 0x10208ULL);

    // Step 4: ERET -> returns to 0x2000
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x2000ULL);

    // Step 5: Executes instruction at 0x2000 now that breakpoint is disabled!
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    CHECK_EQ(cpu.read_register(0), 42ULL); // Executed successfully!
}

// =========================================================================
// Tests 23-24: Target EL Routing via MDCR_EL2.TDE and MDCR_EL3.TDE
// =========================================================================

void test_23_routing_to_el2_via_mdcr_el2_tde() {
    std::cout << "Test 23: Breakpoint routing to EL2 when MDCR_EL2.TDE=1\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Set MDCR_EL2.TDE (bit 8 = 1)
    cpu.set_mdcr_el2(1ULL << 8u);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 1));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    // Routed to EL2: VBAR_EL2 + 0x400 (lower EL vector)
    CHECK_EQ(cpu.get_current_el(), 2u);
    CHECK_EQ(cpu.get_program_counter(), 0x20400ULL); // VBAR_EL2 is 0x20000
    CHECK_EQ(cpu.get_elr_el2(), 0x2000ULL);
    CHECK_EQ(cpu.get_esr_el2() >> 26u, 0x30u); // EC 0x30 = BreakpointLower (from EL1 to EL2)
}

void test_24_routing_to_el3_via_mdcr_el3_tde() {
    std::cout << "Test 24: Breakpoint routing to EL3 when MDCR_EL3.TDE=1\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    // Set MDCR_EL3.TDE (bit 8 = 1)
    cpu.set_mdcr_el3(1ULL << 8u);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 1));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    cpu.step(mem);

    // Routed to EL3: VBAR_EL3 + 0x400 (lower EL vector)
    CHECK_EQ(cpu.get_current_el(), 3u);
    CHECK_EQ(cpu.get_program_counter(), 0x30400ULL); // VBAR_EL3 is 0x30000
    CHECK_EQ(cpu.get_elr_el3(), 0x2000ULL);
    CHECK_EQ(cpu.get_esr_el3() >> 26u, 0x30u); // EC 0x30 = BreakpointLower (from EL1 to EL3)
}

// =========================================================================
// Tests 25-26: SMP Multi-Core Isolation
// =========================================================================

void test_25_smp_core0_breakpoint_does_not_affect_core1() {
    std::cout << "Test 25: SMP: Core 0 breakpoint does not affect Core 1\n";
    winios::SMPCluster cluster(2, 0x40000);
    Memory& mem = cluster.get_memory();

    CPU& core0 = cluster.get_core(0);
    CPU& core1 = cluster.get_core(1);
    setup_cpu(core0, mem, 1, 0x10000);
    setup_cpu(core1, mem, 1, 0x10000);

    // Both cores point to code at 0x2000
    mem.write_u32(0x2000, enc_movz(0, 88));
    core0.set_program_counter(0x2000);
    core1.set_program_counter(0x2000);

    // Only Core 0 enables breakpoint on 0x2000
    core0.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    core0.get_debug_arch().set_dbgbvr(0, 0x2000);
    core0.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    // Core 1 has no breakpoints enabled
    CHECK(!core1.get_debug_arch().monitor_debug_enabled());

    // Step Core 0: hits breakpoint
    core0.step(mem);
    CHECK_EQ(core0.get_program_counter(), 0x10200ULL);
    CHECK_EQ(core0.read_register(0), 0ULL);

    // Step Core 1: executes normally!
    core1.step(mem);
    CHECK_EQ(core1.get_program_counter(), 0x2004ULL);
    CHECK_EQ(core1.read_register(0), 88ULL);
}

void test_26_smp_independent_breakpoints_core0_and_core1() {
    std::cout << "Test 26: SMP: Independent breakpoints on Core 0 and Core 1\n";
    winios::SMPCluster cluster(2, 0x40000);
    Memory& mem = cluster.get_memory();

    CPU& core0 = cluster.get_core(0);
    CPU& core1 = cluster.get_core(1);
    setup_cpu(core0, mem, 1, 0x10000);
    setup_cpu(core1, mem, 1, 0x10000);

    mem.write_u32(0x2000, enc_movz(0, 11));
    mem.write_u32(0x3000, enc_movz(0, 22));

    // Core 0 breaks on 0x2000
    core0.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    core0.get_debug_arch().set_dbgbvr(0, 0x2000);
    core0.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    // Core 1 breaks on 0x3000
    core1.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    core1.get_debug_arch().set_dbgbvr(0, 0x3000);
    core1.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    // Core 0 at 0x3000 executes normally (its breakpoint is at 0x2000)
    core0.set_program_counter(0x3000);
    core0.step(mem);
    CHECK_EQ(core0.get_program_counter(), 0x3004ULL);
    CHECK_EQ(core0.read_register(0), 22ULL);

    // Core 1 at 0x2000 executes normally (its breakpoint is at 0x3000)
    core1.set_program_counter(0x2000);
    core1.step(mem);
    CHECK_EQ(core1.get_program_counter(), 0x2004ULL);
    CHECK_EQ(core1.read_register(0), 11ULL);

    // Core 0 at 0x2000 hits breakpoint!
    core0.set_program_counter(0x2000);
    core0.step(mem);
    CHECK_EQ(core0.get_program_counter(), 0x10200ULL);

    // Core 1 at 0x3000 hits breakpoint!
    core1.set_program_counter(0x3000);
    core1.step(mem);
    CHECK_EQ(core1.get_program_counter(), 0x10200ULL);
}

// =========================================================================
// Test 27: Software Debugger Coexistence
// =========================================================================

void test_27_software_debugger_coexistence() {
    std::cout << "Test 27: Software debugger step through breakpointed instruction\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    Debugger dbg(cpu, mem);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 42));

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u));

    // Step via host Debugger wrapper
    dbg.step();

    // Host debugger successfully stepped, CPU took architectural breakpoint
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_elr_el1(), 0x2000ULL);

    // Debugger inspect shows state without crash
    std::string info = dbg.inspect_cpu();
    CHECK(!info.empty());
    CHECK(info.find("PC=0x0000000000010200") != std::string::npos);
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 51 Stage 4: Hardware Breakpoint Tests ===\n";

    test_1_slot0_breakpoint_hit();
    test_2_slot1_breakpoint_hit();
    test_3_slot2_breakpoint_hit();
    test_4_slot3_breakpoint_hit();
    test_5_disabled_breakpoint_does_not_trigger();
    test_6_mde_zero_disables_all_breakpoints();
    test_7_non_matching_address_does_not_trigger();
    test_8_unaligned_pc_precedence_over_breakpoint();
    test_9_breakpoint_precedence_over_mmu_translation_fault();
    test_10_instruction_suppression_registers_unchanged();
    test_11_instruction_suppression_pc_does_not_advance_past();
    test_12_elr_el1_set_to_instruction_address();
    test_13_esr_ec_lower_el();
    test_14_esr_ec_same_el();
    test_15_spsr_el1_saves_pre_exception_pstate();
    test_16_privilege_filtering_pmc_00_matches_all_els();
    test_17_privilege_filtering_pmc_01_matches_only_el1();
    test_18_privilege_filtering_pmc_10_matches_only_el0();
    test_19_privilege_filtering_pmc_11_matches_el0_and_el1();
    test_20_lowest_numbered_slot_matches_first();
    test_21_eret_reexecutes_if_breakpoint_not_disabled();
    test_22_breakpoint_disable_allows_instruction_to_execute();
    test_23_routing_to_el2_via_mdcr_el2_tde();
    test_24_routing_to_el3_via_mdcr_el3_tde();
    test_25_smp_core0_breakpoint_does_not_affect_core1();
    test_26_smp_independent_breakpoints_core0_and_core1();
    test_27_software_debugger_coexistence();

    std::cout << "\n==================================================\n";
    std::cout << "Hardware Breakpoint Tests Summary:\n";
    std::cout << "  Passed checks: " << g_checks_passed << "\n";
    std::cout << "  Failed checks: " << g_checks_failed << "\n";
    std::cout << "==================================================\n";

    if (g_checks_failed > 0) {
        std::cerr << "Hardware Breakpoint tests FAILED!\n";
        return 1;
    }

    std::cout << "All Hardware Breakpoint tests PASSED successfully!\n";
    return 0;
}
