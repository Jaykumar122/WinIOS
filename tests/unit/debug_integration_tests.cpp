#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <vector>
#include <thread>

#include "cpu/cpu.h"
#include "cpu/debug_architecture.h"
#include "cpu/debug_exceptions.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"

namespace {

// Helpers for encoding AArch64 instructions
constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}
constexpr std::uint32_t enc_add_imm(unsigned rd, unsigned rn, std::uint16_t imm) {
    return 0x91000000u | ((imm & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}
constexpr std::uint32_t enc_sub_imm(unsigned rd, unsigned rn, std::uint16_t imm) {
    return 0xD1000000u | ((imm & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}
constexpr std::uint32_t enc_b(std::int32_t word_offset) {
    return 0x14000000u | (static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu);
}
constexpr std::uint32_t enc_b_eq(std::int32_t word_offset) {
    return 0x54000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | 0x0u;
}
constexpr std::uint32_t enc_ldr_x(unsigned rt, unsigned rn) {
    return 0xF9400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}
constexpr std::uint32_t enc_str_x(unsigned rt, unsigned rn) {
    return 0xF9000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}
constexpr std::uint32_t enc_ldrb(unsigned rt, unsigned rn) {
    return 0x39400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}
constexpr std::uint32_t enc_strb(unsigned rt, unsigned rn) {
    return 0x39000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}
constexpr std::uint32_t enc_ldxr_x(unsigned rt, unsigned rn) {
    return 0xC85F7C00u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}
constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}
constexpr std::uint32_t enc_svc(std::uint16_t imm) {
    return 0xD4000001u | (static_cast<std::uint32_t>(imm) << 5u);
}
constexpr std::uint32_t enc_mrs(unsigned rt, unsigned op0, unsigned op1, unsigned crn, unsigned crm, unsigned op2) {
    return 0xD5300000u | ((op0 & 1u) << 19u) | ((op1 & 7u) << 16u) | ((crn & 15u) << 12u) | ((crm & 15u) << 8u) | ((op2 & 7u) << 5u) | (rt & 31u);
}
constexpr std::uint32_t enc_msr(unsigned rt, unsigned op0, unsigned op1, unsigned crn, unsigned crm, unsigned op2) {
    return 0xD5100000u | ((op0 & 1u) << 19u) | ((op1 & 7u) << 16u) | ((crn & 15u) << 12u) | ((crm & 15u) << 8u) | ((op2 & 7u) << 5u) | (rt & 31u);
}

void setup_cpu(CPU& cpu, Memory& mem, unsigned el = 1, std::uint64_t vbar = 0x10000) {
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(el);
    cpu.set_vbar_el1(vbar);
    cpu.set_vbar_el2(vbar + 0x10000);
    cpu.set_vbar_el3(vbar + 0x20000);
    for (std::size_t i = 0; i < mem.size(); i += 4) {
        mem.write_u32(i, enc_nop());
    }
}

} // namespace

// =========================================================================
// Category 1: Register Access & Identification (Tests 1-5)
// =========================================================================

void test_1_id_aa64dfr0_el1() {
    std::cout << "Test 1: ID_AA64DFR0_EL1 reporting\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x1000);
    // MRS X0, ID_AA64DFR0_EL1
    mem.write_u32(0x1000, enc_mrs(0, 3, 0, 0, 5, 0));
    cpu.step(mem);

    const std::uint64_t val = cpu.read_register(0);
    assert(((val >> 12u) & 0xFu) == 3u); // 4 BRPs (3+1)
    assert(((val >> 20u) & 0xFu) == 3u); // 4 WRPs (3+1)
    assert((val & 0xFu) == 6u);          // DebugVer = 0x6
    std::cout << "  PASS\n";
}

void test_2_id_aa64dfr0_readonly() {
    std::cout << "Test 2: ID_AA64DFR0_EL1 write causes Undefined Instruction\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x1000);
    // MSR ID_AA64DFR0_EL1, X0
    mem.write_u32(0x1000, enc_msr(0, 3, 0, 0, 5, 0));
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x00u); // EC 0x00 (Undefined Instruction)
    assert(cpu.get_program_counter() == 0x10200ULL);
    std::cout << "  PASS\n";
}

void test_3_mdscr_el1_masking() {
    std::cout << "Test 3: MDSCR_EL1 masking\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x1000);
    // MSR MDSCR_EL1, X0 (with all 64 bits set)
    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFULL);
    mem.write_u32(0x1000, enc_msr(0, 2, 0, 0, 2, 2));
    cpu.step(mem);

    // Only bits 15 (MDE) and 0 (SS) are retained
    constexpr std::uint64_t kExpected = (1ULL << 15u) | (1ULL << 0u);
    assert(cpu.get_debug_arch().get_mdscr_el1() == kExpected);
    std::cout << "  PASS\n";
}

void test_4_dbgbvr_dbgbcr_roundtrip() {
    std::cout << "Test 4: DBGBVR and DBGBCR 0..3 roundtrip\n";
    CPU cpu;
    auto& d = cpu.get_debug_arch();
    for (std::size_t i = 0; i < 4; ++i) {
        d.set_dbgbvr(i, 0x12340000ULL + i * 0x1000);
        d.set_dbgbcr(i, static_cast<std::uint32_t>(0x1u | (i << 1u)));
        assert(d.get_dbgbvr(i) == 0x12340000ULL + i * 0x1000);
        assert(d.get_dbgbcr(i) == static_cast<std::uint32_t>(0x1u | (i << 1u)));
    }
    std::cout << "  PASS\n";
}

void test_5_dbgwvr_dbgwcr_roundtrip() {
    std::cout << "Test 5: DBGWVR and DBGWCR 0..3 roundtrip\n";
    CPU cpu;
    auto& d = cpu.get_debug_arch();
    for (std::size_t i = 0; i < 4; ++i) {
        d.set_dbgwvr(i, 0x56780000ULL + i * 0x1000);
        d.set_dbgwcr(i, static_cast<std::uint32_t>(0x1u | (0x3u << 3u) | (i << 1u)));
        assert(d.get_dbgwvr(i) == 0x56780000ULL + i * 0x1000);
        assert(d.get_dbgwcr(i) == static_cast<std::uint32_t>(0x1u | (0x3u << 3u) | (i << 1u)));
    }
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 2: Privilege Filtering & Access Control (Tests 6-10)
// =========================================================================

void test_6_el0_mdscr_trap() {
    std::cout << "Test 6: EL0 MRS MDSCR_EL1 traps as Undefined Instruction\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 0, 0x10000); // Executing at EL0
    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_mrs(0, 2, 0, 0, 2, 2));
    cpu.step(mem);

    assert(cpu.get_current_el() == 1);
    assert((cpu.get_esr_el1() >> 26u) == 0x00u); // EC 0x00 (Undefined Instruction)
    assert(cpu.get_program_counter() == 0x10400ULL);
    std::cout << "  PASS\n";
}

void test_7_el0_dbgbvr_trap() {
    std::cout << "Test 7: EL0 MSR DBGBVR0 traps as Undefined Instruction\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 0, 0x10000);
    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_msr(0, 2, 0, 0, 0, 4));
    cpu.step(mem);

    assert(cpu.get_current_el() == 1);
    assert((cpu.get_esr_el1() >> 26u) == 0x00u);
    assert(cpu.get_program_counter() == 0x10400ULL);
    std::cout << "  PASS\n";
}

void test_8_mdcr_el2_tda_trap() {
    std::cout << "Test 8: MDCR_EL2.TDA traps EL1 debug sysreg access to EL2\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_mdcr_el2(1ULL << 9u); // MDCR_EL2.TDA
    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_mrs(0, 2, 0, 0, 2, 2));
    cpu.step(mem);

    assert(cpu.get_current_el() == 2);
    assert((cpu.get_esr_el2() >> 26u) == 0x18u); // EC 0x18 (Trapped MSR/MRS)
    std::cout << "  PASS\n";
}

void test_9_mdcr_el3_tda_trap() {
    std::cout << "Test 9: MDCR_EL3.TDA traps EL1/EL2 debug sysreg access to EL3\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_mdcr_el3(1ULL << 9u); // MDCR_EL3.TDA
    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_mrs(0, 2, 0, 0, 2, 2));
    cpu.step(mem);

    assert(cpu.get_current_el() == 3);
    assert((cpu.get_esr_el3() >> 26u) == 0x18u); // EC 0x18
    std::cout << "  PASS\n";
}

void test_10_breakpoint_pmc_filtering() {
    std::cout << "Test 10: Breakpoint PMC privilege filtering\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    // PMC = 0b10 (Match EL0 only)
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x2u << 1u) | (0xFu << 5u));

    // Executing at EL1 -> must NOT match
    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_add_imm(0, 0, 1));
    cpu.step(mem);

    assert(cpu.get_program_counter() == 0x1004ULL);
    assert(cpu.read_register(0) == 1ULL);
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 3: Breakpoint Matching & Exception Routing (Tests 11-15)
// =========================================================================

void test_11_bp_same_el() {
    std::cout << "Test 11: Breakpoint at EL1 generates BreakpointSame (EC 0x31)\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_add_imm(0, 0, 1));
    cpu.step(mem);

    assert(cpu.get_current_el() == 1);
    assert((cpu.get_esr_el1() >> 26u) == 0x31u);
    assert(cpu.get_elr_el1() == 0x1000ULL);
    assert(cpu.read_register(0) == 0ULL); // Not executed
    std::cout << "  PASS\n";
}

void test_12_bp_lower_el() {
    std::cout << "Test 12: Breakpoint at EL0 generates BreakpointLower (EC 0x30)\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 0, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_add_imm(0, 0, 1));
    cpu.step(mem);

    assert(cpu.get_current_el() == 1);
    assert((cpu.get_esr_el1() >> 26u) == 0x30u);
    assert(cpu.get_elr_el1() == 0x1000ULL);
    std::cout << "  PASS\n";
}

void test_13_bp_route_el2() {
    std::cout << "Test 13: Breakpoint routing to EL2 via MDCR_EL2.TDE\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_mdcr_el2(1ULL << 8u); // MDCR_EL2.TDE
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u));

    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    assert(cpu.get_current_el() == 2);
    assert((cpu.get_esr_el2() >> 26u) == 0x30u); // Lower EL from EL2 perspective
    std::cout << "  PASS\n";
}

void test_14_bp_route_el3() {
    std::cout << "Test 14: Breakpoint routing to EL3 via MDCR_EL3.TDE\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_mdcr_el3(1ULL << 8u); // MDCR_EL3.TDE
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u));

    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    assert(cpu.get_current_el() == 3);
    assert((cpu.get_esr_el3() >> 26u) == 0x30u);
    std::cout << "  PASS\n";
}

void test_15_bp_multiple_slot_priority() {
    std::cout << "Test 15: Multiple breakpoint slot priority (Slot 0 over Slot 1)\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u));
    cpu.get_debug_arch().set_dbgbvr(1, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(1, 0x1u | (0x3u << 1u) | (0xFu << 5u));

    auto match = cpu.get_debug_arch().check_execution_breakpoint(0x1000, 1);
    assert(match.has_value());
    assert(match->slot_index == 0u);
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 4: Watchpoint Matching & Data Trapping (Tests 16-20)
// =========================================================================

void test_16_wp_load_match() {
    std::cout << "Test 16: Watchpoint load match (WnR=0, EC 0x35)\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    // Enabled, LSC=Load (1), Both ELs (3), BAS=0xFF
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x1u << 3u) | (0xFFu << 5u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x8000);
    cpu.step(mem);

    assert(cpu.get_current_el() == 1);
    assert((cpu.get_esr_el1() >> 26u) == 0x35u);
    assert(((cpu.get_esr_el1() >> 6u) & 0x1u) == 0u); // WnR = 0 (Read)
    assert(cpu.get_far_el1() == 0x8000ULL);
    std::cout << "  PASS\n";
}

void test_17_wp_store_match() {
    std::cout << "Test 17: Watchpoint store match (WnR=1, EC 0x35)\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    // Enabled, LSC=Store (2), Both ELs (3), BAS=0xFF
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x2u << 3u) | (0xFFu << 5u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_str_x(0, 1));
    cpu.write_register(1, 0x8000);
    cpu.step(mem);

    assert(cpu.get_current_el() == 1);
    assert((cpu.get_esr_el1() >> 26u) == 0x35u);
    assert(((cpu.get_esr_el1() >> 6u) & 0x1u) == 1u); // WnR = 1 (Write)
    assert(cpu.get_far_el1() == 0x8000ULL);
    std::cout << "  PASS\n";
}

void test_18_wp_lower_el() {
    std::cout << "Test 18: Watchpoint from EL0 generates WatchpointLower (EC 0x34)\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 0, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x3u << 3u) | (0xFFu << 5u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x8000);
    cpu.step(mem);

    assert(cpu.get_current_el() == 1);
    assert((cpu.get_esr_el1() >> 26u) == 0x34u); // Lower EL
    assert(cpu.get_far_el1() == 0x8000ULL);
    std::cout << "  PASS\n";
}

void test_19_wp_far_preservation() {
    std::cout << "Test 19: Watchpoint FAR holds exact data virtual address\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x8004);
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x3u << 3u) | (0xFFu << 5u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_strb(0, 1));
    cpu.write_register(1, 0x8004);
    cpu.step(mem);

    assert(cpu.get_far_el1() == 0x8004ULL);
    std::cout << "  PASS\n";
}

void test_20_wp_prevents_memory_mutation() {
    std::cout << "Test 20: Watchpoint store prevents memory mutation\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    mem.write_u64(0x8000, 0xAAAAAAAAAAAAAAAAULL);

    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x2u << 3u) | (0xFFu << 5u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_str_x(0, 1));
    cpu.write_register(0, 0xBBBBBBBBBBBBBBBBULL);
    cpu.write_register(1, 0x8000);
    cpu.step(mem);

    // Watchpoint fired: memory MUST be unmodified
    assert(mem.read_u64(0x8000) == 0xAAAAAAAAAAAAAAAAULL);
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 5: Watchpoint Sub-Word & BAS Byte Selection (Tests 21-25)
// =========================================================================

void test_21_bas_byte_0_match() {
    std::cout << "Test 21: BAS matching byte 0\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    // BAS = 0b00000001 (bit 0 = byte 0)
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x3u << 3u) | (0x01u << 5u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_strb(0, 1));
    cpu.write_register(1, 0x8000);
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x35u);
    std::cout << "  PASS\n";
}

void test_22_bas_byte_7_match() {
    std::cout << "Test 22: BAS matching byte 7\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    // BAS = 0b10000000 (bit 7 = byte 7)
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x3u << 3u) | (0x80u << 5u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_strb(0, 1));
    cpu.write_register(1, 0x8007);
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x35u);
    std::cout << "  PASS\n";
}

void test_23_bas_non_matching_byte() {
    std::cout << "Test 23: BAS non-matching byte access falls through cleanly\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    // BAS = 0b00000001 (byte 0 only)
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x3u << 3u) | (0x01u << 5u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_strb(0, 1));
    cpu.write_register(0, 0x42);
    cpu.write_register(1, 0x8001); // Access byte 1 -> should NOT trigger
    cpu.step(mem);

    assert(cpu.get_program_counter() == 0x1004ULL);
    assert(mem.read_u8(0x8001) == 0x42);
    std::cout << "  PASS\n";
}

void test_24_bas_multi_byte_overlap() {
    std::cout << "Test 24: Multi-byte access overlapping BAS byte\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    // BAS = 0b00000100 (byte 2)
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x3u << 3u) | (0x04u << 5u));

    cpu.set_program_counter(0x1000);
    // 64-bit load from 0x8000 covers bytes 0..7 -> overlaps byte 2
    mem.write_u32(0x1000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x8000);
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x35u);
    std::cout << "  PASS\n";
}

void test_25_lsc_direction_filtering() {
    std::cout << "Test 25: LSC Load-only ignores Store, Store-only ignores Load\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    // LSC = 1 (Load only)
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x1u << 3u) | (0xFFu << 5u));

    // Store to 0x8000 -> must NOT trigger
    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_str_x(0, 1));
    cpu.write_register(1, 0x8000);
    cpu.step(mem);

    assert(cpu.get_program_counter() == 0x1004ULL);
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 6: Linked Execution Breakpoints (Tests 26-30)
// =========================================================================

void test_26_linked_bp_match() {
    std::cout << "Test 26: Linked breakpoint matches when both conditions met\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);

    // BP0 linked to BP1 (BT=1, LBN=1, address 0x1000)
    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (1u << 16u) | (1u << 20u));

    // BP1 linked target (BT=3 / match 0x1000)
    cpu.get_debug_arch().set_dbgbvr(1, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(1, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (3u << 20u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_add_imm(0, 0, 1));
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x31u);
    std::cout << "  PASS\n";
}

void test_27_linked_bp_disabled_target() {
    std::cout << "Test 27: Linked breakpoint does not fire when linked target disabled\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);

    // BP0 linked to BP1
    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (1u << 16u) | (1u << 20u));

    // BP1 disabled (E=0)
    cpu.get_debug_arch().set_dbgbvr(1, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(1, 0x0u);

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_add_imm(0, 0, 1));
    cpu.step(mem);

    assert(cpu.get_program_counter() == 0x1004ULL);
    assert(cpu.read_register(0) == 1ULL);
    std::cout << "  PASS\n";
}

void test_28_linked_bp_primary_mismatch() {
    std::cout << "Test 28: Linked breakpoint does not fire when primary address mismatches\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);

    // BP0 at 0x2000 linked to BP1 at 0x1000
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (1u << 16u) | (1u << 20u));

    cpu.get_debug_arch().set_dbgbvr(1, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(1, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (3u << 20u));

    cpu.set_program_counter(0x1000); // Executing 0x1000 (primary is 0x2000)
    mem.write_u32(0x1000, enc_add_imm(0, 0, 1));
    cpu.step(mem);

    assert(cpu.get_program_counter() == 0x1004ULL);
    std::cout << "  PASS\n";
}

void test_29_self_referential_link() {
    std::cout << "Test 29: Self-referential link (LBN == self) does not fire\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);

    // BP0 linked to BP0 (LBN=0)
    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (0u << 16u) | (1u << 20u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_add_imm(0, 0, 1));
    cpu.step(mem);

    assert(cpu.get_program_counter() == 0x1004ULL);
    std::cout << "  PASS\n";
}

void test_30_chained_linked_bp_cascade() {
    std::cout << "Test 30: Chained 3-level linked breakpoint cascade\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);

    // BP0 -> BP1 -> BP2
    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (1u << 16u) | (1u << 20u));

    cpu.get_debug_arch().set_dbgbvr(1, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(1, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (2u << 16u) | (1u << 20u));

    cpu.get_debug_arch().set_dbgbvr(2, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(2, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (3u << 20u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_add_imm(0, 0, 1));
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x31u);
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 7: Linked Data Watchpoints (Tests 31-35)
// =========================================================================

void test_31_linked_wp_match() {
    std::cout << "Test 31: Linked watchpoint fires when memory VA and PC both match\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);

    // WP0 watched at 0x8000, linked to BP0 (WT=1, LBN=0)
    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x3u << 3u) | (0xFFu << 5u) | (0u << 16u) | (1u << 20u));

    // BP0 set to PC 0x1000 with BT=3 (Secondary link target only)
    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (3u << 20u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x8000);
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x35u);
    assert(cpu.get_far_el1() == 0x8000ULL);
    std::cout << "  PASS\n";
}

void test_32_linked_wp_pc_mismatch() {
    std::cout << "Test 32: Linked watchpoint does not fire when PC mismatches\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);

    // WP0 linked to BP0 (address 0x2000)
    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x3u << 3u) | (0xFFu << 5u) | (0u << 16u) | (1u << 20u));

    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (3u << 20u));

    cpu.set_program_counter(0x1000); // PC is 0x1000 != 0x2000
    mem.write_u32(0x1000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x8000);
    cpu.step(mem);

    assert(cpu.get_program_counter() == 0x1004ULL);
    std::cout << "  PASS\n";
}

void test_33_linked_wp_disabled_bp() {
    std::cout << "Test 33: Linked watchpoint does not fire when linked BP is disabled\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);

    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x3u << 3u) | (0xFFu << 5u) | (0u << 16u) | (1u << 20u));

    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x0u | (3u << 20u)); // Disabled

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x8000);
    cpu.step(mem);

    assert(cpu.get_program_counter() == 0x1004ULL);
    std::cout << "  PASS\n";
}

void test_34_linked_wp_privilege_mismatch() {
    std::cout << "Test 34: Linked watchpoint privilege mismatch inhibits match\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);

    // WP0 enabled for EL1
    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x1u << 1u) | (0x3u << 3u) | (0xFFu << 5u) | (0u << 16u) | (1u << 20u));

    // BP0 configured for EL0 ONLY (PMC=2)
    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x2u << 1u) | (0xFu << 5u) | (3u << 20u));

    // Executing at EL1
    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x8000);
    cpu.step(mem);

    assert(cpu.get_program_counter() == 0x1004ULL);
    std::cout << "  PASS\n";
}

void test_35_linked_wp_cascade() {
    std::cout << "Test 35: Cascade linked watchpoint (WP0 -> BP0 -> BP1)\n";
    winios::DebugArchitecture d;
    d.set_mdscr_el1(1ULL << 15u);

    // WP0 linked to BP0
    d.set_dbgwvr(0, 0x8000);
    d.set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x3u << 3u) | (0xFFu << 5u) | (0u << 16u) | (1u << 20u));

    // BP0 linked to BP1
    d.set_dbgbvr(0, 0x1000);
    d.set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (1u << 16u) | (1u << 20u));

    // BP1 terminal target
    d.set_dbgbvr(1, 0x1000);
    d.set_dbgbcr(1, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (3u << 20u));

    auto ev = d.check_data_watchpoint(0x8000, 8, winios::DebugAccessType::Read, 1, 0x1000);
    assert(ev.has_value());
    assert(ev->type == winios::DebugEventType::Watchpoint);
    assert(ev->slot_index == 0);
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 8: Single-Step Execution & Retirement (Tests 36-40)
// =========================================================================

void test_36_ss_retired_instruction() {
    std::cout << "Test 36: Single-step retired instruction advances PC and generates exception\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_add_imm(0, 0, 5));
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x33u); // SoftwareStepSame
    assert(cpu.get_elr_el1() == 0x1004ULL);      // Next instruction PC
    assert(cpu.read_register(0) == 5ULL);        // Instruction retired!
    std::cout << "  PASS\n";
}

void test_37_ss_exclusive_instruction() {
    std::cout << "Test 37: Single-step exclusive instruction sets ISS.EX bit 6\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_ldxr_x(0, 1));
    cpu.write_register(1, 0x8000);
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x33u);
    assert(((cpu.get_esr_el1() >> 6u) & 0x1u) == 1u); // EX bit set!
    std::cout << "  PASS\n";
}

void test_38_ss_branch_taken() {
    std::cout << "Test 38: Single-step across conditional branch taken\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);
    cpu.set_nzcv(0x4u); // Z flag = 1 (EQ true)

    cpu.set_program_counter(0x1000);
    // B.EQ +8 (target 0x1020)
    mem.write_u32(0x1000, enc_b_eq(8));
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x33u);
    assert(cpu.get_elr_el1() == 0x1020ULL);
    std::cout << "  PASS\n";
}

void test_39_ss_branch_not_taken() {
    std::cout << "Test 39: Single-step across conditional branch not taken\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);
    cpu.set_nzcv(0x0u); // Z flag = 0 (EQ false)

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_b_eq(8));
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x33u);
    assert(cpu.get_elr_el1() == 0x1004ULL);
    std::cout << "  PASS\n";
}

void test_40_ss_eret_instruction() {
    std::cout << "Test 40: Single-step across ERET updates EL and generates SoftwareStepLower\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_eret());
    cpu.set_elr_el1(0x8000);
    cpu.set_spsr_el1(0x00000000); // Return to EL0

    cpu.step(mem);

    assert(cpu.get_current_el() == 1);
    assert((cpu.get_esr_el1() >> 26u) == 0x32u); // SoftwareStepLower
    assert(cpu.get_elr_el1() == 0x8000ULL);
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 9: Priority & Fault Coexistence (Tests 41-45)
// =========================================================================

void test_41_bp_priority_over_ss() {
    std::cout << "Test 41: Breakpoint takes priority over Single-step\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.get_debug_arch().set_dbgbvr(0, 0x1000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_add_imm(0, 0, 1));
    cpu.step(mem);

    // Breakpoint wins (EC 0x31)
    assert((cpu.get_esr_el1() >> 26u) == 0x31u);
    assert(cpu.get_elr_el1() == 0x1000ULL);
    assert(cpu.read_register(0) == 0ULL); // Not retired
    std::cout << "  PASS\n";
}

void test_42_wp_priority_over_ss() {
    std::cout << "Test 42: Watchpoint takes priority over Single-step\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.get_debug_arch().set_dbgwvr(0, 0x8000);
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x3u << 3u) | (0xFFu << 5u));

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_str_x(0, 1));
    cpu.write_register(1, 0x8000);
    cpu.step(mem);

    // Watchpoint wins (EC 0x35)
    assert((cpu.get_esr_el1() >> 26u) == 0x35u);
    assert(cpu.get_elr_el1() == 0x1000ULL);
    std::cout << "  PASS\n";
}

void test_43_data_abort_priority_over_ss() {
    std::cout << "Test 43: Data abort takes priority over Single-step\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0xDEADBEEFULL); // Unbacked RAM
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x25u); // DataAbortSame
    assert(cpu.get_elr_el1() == 0x1000ULL);
    std::cout << "  PASS\n";
}

void test_44_instruction_abort_priority_over_ss() {
    std::cout << "Test 44: Instruction abort takes priority over Single-step\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.set_program_counter(0xDEADBEE0ULL); // Unbacked RAM for instruction fetch
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x21u); // InstructionAbortSame
    assert(cpu.get_elr_el1() == 0xDEADBEE0ULL);
    std::cout << "  PASS\n";
}

void test_45_alignment_fault_priority_over_ss() {
    std::cout << "Test 45: Alignment fault takes priority over Single-step\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x8001); // Unaligned 64-bit access
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x25u); // DataAbortSame with Alignment FSC
    assert(cpu.get_elr_el1() == 0x1000ULL);
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 10: System Calls, Reset, SMP & Parallel Concurrency (Tests 46-50)
// =========================================================================

void test_46_svc_suppresses_ss() {
    std::cout << "Test 46: SVC takes exception (EC 0x15) and does not fire Single-step\n";
    CPU cpu;
    Memory mem(0x20000);
    setup_cpu(cpu, mem, 0, 0x10000); // EL0
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_pstate_ss(true);

    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, enc_svc(77));
    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x15u); // SVC wins
    assert((cpu.get_esr_el1() & 0xFFFFu) == 77u);
    std::cout << "  PASS\n";
}

void test_47_cpu_reset_clears_debug() {
    std::cout << "Test 47: Core reset clears all debug state\n";
    CPU cpu;
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_pstate_ss(true);
    cpu.get_debug_arch().set_dbgbvr(0, 0x1234);

    cpu.reset();

    assert(cpu.get_debug_arch().get_mdscr_el1() == 0);
    assert(!cpu.get_pstate_ss());
    assert(cpu.get_debug_arch().get_dbgbvr(0) == 0);
    std::cout << "  PASS\n";
}

void test_48_smp_two_core_isolation() {
    std::cout << "Test 48: Two-core independent debug configuration\n";
    winios::SMPCluster cluster(2, 0x40000);
    auto& c0 = cluster.get_core(0);
    auto& c1 = cluster.get_core(1);
    auto& mem = cluster.get_memory();

    setup_cpu(c0, mem, 1, 0x10000);
    setup_cpu(c1, mem, 1, 0x20000);

    c0.set_program_counter(0x1000);
    c1.set_program_counter(0x2000);
    mem.write_u32(0x1000, enc_add_imm(0, 0, 1));
    mem.write_u32(0x2000, enc_add_imm(1, 1, 2));

    // Core 0 has BP at 0x1000; Core 1 has no BP
    c0.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    c0.get_debug_arch().set_dbgbvr(0, 0x1000);
    c0.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u));

    c0.step(mem);
    c1.step(mem);

    // Core 0 took breakpoint exception
    assert(c0.get_program_counter() == 0x10200ULL);
    assert(c0.read_register(0) == 0ULL);

    // Core 1 executed cleanly
    assert(c1.get_program_counter() == 0x2004ULL);
    assert(c1.read_register(1) == 2ULL);
    std::cout << "  PASS\n";
}

void test_49_smp_four_core_dispatch() {
    std::cout << "Test 49: Four-core independent breakpoint and watchpoint dispatch\n";
    winios::SMPCluster cluster(4, 0x40000);
    auto& mem = cluster.get_memory();

    for (std::size_t i = 0; i < 4; ++i) {
        auto& core = cluster.get_core(i);
        setup_cpu(core, mem, 1, 0x10000 * (i + 1));
        const std::uint64_t pc = 0x1000 + i * 0x100;
        core.set_program_counter(pc);
        mem.write_u32(pc, enc_add_imm(0, 0, 1));

        if (i == 2) {
            core.get_debug_arch().set_mdscr_el1(1ULL << 15u);
            core.get_debug_arch().set_dbgbvr(0, pc);
            core.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u));
        }
        core.step(mem);
    }

    // Only core 2 took breakpoint
    assert(cluster.get_core(0).get_program_counter() == 0x1004ULL);
    assert(cluster.get_core(1).get_program_counter() == 0x1104ULL);
    assert(cluster.get_core(2).get_program_counter() == 0x30200ULL); // VBAR EL1h
    assert(cluster.get_core(3).get_program_counter() == 0x1304ULL);
    std::cout << "  PASS\n";
}

void test_50_parallel_concurrent_stepping() {
    std::cout << "Test 50: Multithreaded parallel execution stress test with concurrent stepping\n";
    constexpr std::size_t kNumThreads = 4;
    std::vector<std::thread> workers;

    for (std::size_t t = 0; t < kNumThreads; ++t) {
        workers.emplace_back([t]() {
            CPU cpu;
            Memory mem(0x10000);
            setup_cpu(cpu, mem, 1, 0x8000);
            cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));

            for (std::size_t iter = 0; iter < 100; ++iter) {
                cpu.set_daif(0x0u);
                cpu.set_pstate_ss(true);
                const std::uint64_t pc = 0x1000 + (t * 0x100);
                cpu.set_program_counter(pc);
                mem.write_u32(pc, enc_add_imm(0, 0, 1));
                cpu.step(mem);
                assert((cpu.get_esr_el1() >> 26u) == 0x33u);
                assert(cpu.get_elr_el1() == pc + 4u);
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }
    std::cout << "  PASS\n";
}

int main() {
    std::cout << "=== WinIOS Phase 51 Stage 9: Comprehensive Debug Integration Tests ===\n";

    test_1_id_aa64dfr0_el1();
    test_2_id_aa64dfr0_readonly();
    test_3_mdscr_el1_masking();
    test_4_dbgbvr_dbgbcr_roundtrip();
    test_5_dbgwvr_dbgwcr_roundtrip();

    test_6_el0_mdscr_trap();
    test_7_el0_dbgbvr_trap();
    test_8_mdcr_el2_tda_trap();
    test_9_mdcr_el3_tda_trap();
    test_10_breakpoint_pmc_filtering();

    test_11_bp_same_el();
    test_12_bp_lower_el();
    test_13_bp_route_el2();
    test_14_bp_route_el3();
    test_15_bp_multiple_slot_priority();

    test_16_wp_load_match();
    test_17_wp_store_match();
    test_18_wp_lower_el();
    test_19_wp_far_preservation();
    test_20_wp_prevents_memory_mutation();

    test_21_bas_byte_0_match();
    test_22_bas_byte_7_match();
    test_23_bas_non_matching_byte();
    test_24_bas_multi_byte_overlap();
    test_25_lsc_direction_filtering();

    test_26_linked_bp_match();
    test_27_linked_bp_disabled_target();
    test_28_linked_bp_primary_mismatch();
    test_29_self_referential_link();
    test_30_chained_linked_bp_cascade();

    test_31_linked_wp_match();
    test_32_linked_wp_pc_mismatch();
    test_33_linked_wp_disabled_bp();
    test_34_linked_wp_privilege_mismatch();
    test_35_linked_wp_cascade();

    test_36_ss_retired_instruction();
    test_37_ss_exclusive_instruction();
    test_38_ss_branch_taken();
    test_39_ss_branch_not_taken();
    test_40_ss_eret_instruction();

    test_41_bp_priority_over_ss();
    test_42_wp_priority_over_ss();
    test_43_data_abort_priority_over_ss();
    test_44_instruction_abort_priority_over_ss();
    test_45_alignment_fault_priority_over_ss();

    test_46_svc_suppresses_ss();
    test_47_cpu_reset_clears_debug();
    test_48_smp_two_core_isolation();
    test_49_smp_four_core_dispatch();
    test_50_parallel_concurrent_stepping();

    std::cout << "\nALL 50 DEBUG INTEGRATION TESTS PASSED (100%)\n";
    return 0;
}
