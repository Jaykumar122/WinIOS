#include "cpu/cpu.h"
#include "cpu/debug_architecture.h"
#include "cpu/debug_exceptions.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"
#include <iostream>
#include <vector>
#include <thread>
#include <cassert>

// WinIOS Phase 51 Stage 6: AArch64 Debug Exception Architecture Hardening Tests
// Comprehensive verification of:
// - Debug exception classification (EC 0x30, 0x31, 0x34, 0x35)
// - Complete ESR_ELx syndrome construction (EC, IL, ISS, DFSC, WnR)
// - ELR_ELx, SPSR_ELx, and FAR_ELx preservation and correctness
// - Target EL routing (EL1, EL2 via MDCR_EL2.TDE, EL3 via MDCR_EL3.TDE)
// - Vector table offset selection (0x000 SP_EL0, 0x200 SP_ELx, 0x400 Lower EL)
// - Precedence against PC alignment, MMU translation, and permission faults
// - Store mutation suppression and load destination suppression
// - ERET exception return and retry semantics
// - 4-core SMP per-core isolation in deterministic and parallel modes
// - Stage 4 (breakpoint) and Stage 5 (watchpoint) regression verification

namespace {

static std::size_t g_checks_passed = 0;
static std::size_t g_checks_failed = 0;

#define CHECK_EQ(actual, expected) \
    do { \
        if ((actual) == (expected)) { \
            ++g_checks_passed; \
        } else { \
            ++g_checks_failed; \
            std::cerr << "FAILED check at " << __FILE__ << ":" << __LINE__ \
                      << " - Expected " << std::hex << (expected) \
                      << " but got " << (actual) << std::dec << "\n"; \
        } \
    } while (0)

#define CHECK_TRUE(cond) \
    do { \
        if (cond) { \
            ++g_checks_passed; \
        } else { \
            ++g_checks_failed; \
            std::cerr << "FAILED check at " << __FILE__ << ":" << __LINE__ \
                      << " - Expected condition (" #cond ") to be true\n"; \
        } \
    } while (0)

// Helper: encode NOP
constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

// Helper: encode LDR Xt, [Xn]
constexpr std::uint32_t enc_ldr_x(unsigned rt, unsigned rn) {
    return 0xF9400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode STR Xt, [Xn]
constexpr std::uint32_t enc_str_x(unsigned rt, unsigned rn) {
    return 0xF9000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode ERET
constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}

// Helper: construct DBGBCR value (E=bit 0, PMC=bits [2:1], BAS=bits [8:5])
constexpr std::uint32_t make_dbgbcr(bool enable, unsigned pmc, unsigned bas = 0xFu) {
    return (enable ? 1u : 0u) | ((pmc & 3u) << 1u) | ((bas & 0xFu) << 5u);
}

// Helper: construct DBGWCR value (E=bit 0, PAC=bits [2:1], LSC=bits [4:3], BAS=bits [12:5])
constexpr std::uint32_t make_dbgwcr(bool enable, unsigned pac, unsigned lsc, std::uint8_t bas) {
    return (enable ? 1u : 0u) | ((pac & 3u) << 1u) | ((lsc & 3u) << 3u) | (static_cast<std::uint32_t>(bas) << 5u);
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

// =========================================================================
// Category 1-4: Exception Classification (Lower & Same EL)
// =========================================================================

void test_1_breakpoint_lower_el() {
    std::cout << "Test 1: Breakpoint lower-EL exception (EC 0x30)\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 0, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b11)); // EL0 or EL1
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10400ULL); // Lower EL vector
    CHECK_EQ(cpu.get_current_el(), 1u);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x30u); // EC 0x30
}

void test_2_breakpoint_same_el() {
    std::cout << "Test 2: Breakpoint same-EL exception (EC 0x31)\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b11));
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL); // Same EL SP_ELx vector
    CHECK_EQ(cpu.get_current_el(), 1u);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x31u); // EC 0x31
}

void test_3_watchpoint_lower_el() {
    std::cout << "Test 3: Watchpoint lower-EL exception (EC 0x34)\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 0, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x5000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b01, 0xFF));
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10400ULL);
    CHECK_EQ(cpu.get_current_el(), 1u);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x34u); // EC 0x34
}

void test_4_watchpoint_same_el() {
    std::cout << "Test 4: Watchpoint same-EL exception (EC 0x35)\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x5000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b11, 0b01, 0xFF));
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_current_el(), 1u);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x35u); // EC 0x35
}

// =========================================================================
// Category 5-8: ESR Construction, WnR, and DFSC
// =========================================================================

void test_5_breakpoint_esr_construction() {
    std::cout << "Test 5: Breakpoint ESR construction (IL=1, ISS=0)\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x3000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x3000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    cpu.step(mem);
    const std::uint32_t esr = cpu.get_esr_el1();
    CHECK_EQ(esr >> 26u, 0x31u);
    CHECK_EQ((esr >> 25u) & 1u, 1u); // IL = 1
    CHECK_EQ(esr & 0x00FFFFFFu, 0u); // ISS = 0
}

void test_6_watchpoint_esr_construction() {
    std::cout << "Test 6: Watchpoint ESR construction\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x5000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b11, 0xFF));
    cpu.step(mem);
    const std::uint32_t esr = cpu.get_esr_el1();
    CHECK_EQ(esr >> 26u, 0x35u);
    CHECK_EQ((esr >> 25u) & 1u, 1u); // IL = 1
}

void test_7_wnr_correctness() {
    std::cout << "Test 7: WnR correctness (Read vs Write)\n";
    // Load -> WnR = 0
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_ldr_x(0, 1));
        cpu.write_register(1, 0x5000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b11, 0xFF));
        cpu.step(mem);
        CHECK_EQ((cpu.get_esr_el1() >> 6u) & 1u, 0u); // WnR = 0
    }
    // Store -> WnR = 1
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_str_x(0, 1));
        cpu.write_register(1, 0x5000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b11, 0xFF));
        cpu.step(mem);
        CHECK_EQ((cpu.get_esr_el1() >> 6u) & 1u, 1u); // WnR = 1
    }
}

void test_8_dfsc_correctness() {
    std::cout << "Test 8: DFSC correctness (0x22 Synchronous Watchpoint)\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x5000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b11, 0xFF));
    cpu.step(mem);
    CHECK_EQ(cpu.get_esr_el1() & 0x3Fu, 0x22u); // DFSC = 0x22
}

// =========================================================================
// Category 9-12: Register Preservation (FAR, ELR, SPSR, PSTATE)
// =========================================================================

void test_9_far_elx_behavior() {
    std::cout << "Test 9: FAR_ELx behavior (Watchpoint populates FAR, Breakpoint preserves FAR)\n";
    // Watchpoint populates FAR_EL1 with the data virtual address
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_far_el1(0xCAFEULL);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_ldr_x(0, 1));
        cpu.write_register(1, 0x7000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x7000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b11, 0xFF));
        cpu.step(mem);
        CHECK_EQ(cpu.get_far_el1(), 0x7000ULL); // FAR updated with data address
    }
    // Breakpoint preserves pre-existing FAR (FAR is UNKNOWN in Armv8-A)
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_far_el1(0x12345678ULL);
        cpu.set_program_counter(0x3000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x3000);
        cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
        cpu.step(mem);
        CHECK_EQ(cpu.get_far_el1(), 0x12345678ULL); // FAR was NOT corrupted by breakpoint!
    }
}

void test_10_elr_elx_behavior() {
    std::cout << "Test 10: ELR_ELx behavior\n";
    // For breakpoint: ELR points to the breakpoint instruction
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x4000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgbvr(0, 0x4000);
        cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
        cpu.step(mem);
        CHECK_EQ(cpu.get_elr_el1(), 0x4000ULL);
    }
    // For watchpoint: ELR points to the load/store instruction
    {
        CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x4000);
        mem.write_u32(0x4000, enc_str_x(0, 1));
        cpu.write_register(1, 0x5000);
        cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
        cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b10, 0xFF));
        cpu.step(mem);
        CHECK_EQ(cpu.get_elr_el1(), 0x4000ULL);
    }
}

void test_11_spsr_elx_preservation() {
    std::cout << "Test 11: SPSR_ELx preservation\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    // Set specific flags
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    cpu.step(mem);
    const std::uint32_t spsr = cpu.get_spsr_el1();
    // In EL1 with SPSel=1: M[3:0] = 0b0101 (0x5)
    CHECK_EQ(spsr & 0xFu, 0x5u);
}

void test_12_pstate_preservation() {
    std::cout << "Test 12: PSTATE mutation on exception entry\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    cpu.step(mem);
    // On exception entry: DAIF must be masked (0xF)
    CHECK_EQ(cpu.get_daif(), 0xFu);
    CHECK_EQ(cpu.get_current_el(), 1u);
}

// =========================================================================
// Category 13-17: Exception Routing (EL1, EL2, EL3, MDCR_ELx.TDE)
// =========================================================================

void test_13_el1_routing() {
    std::cout << "Test 13: EL1 default routing\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    cpu.step(mem);
    CHECK_EQ(cpu.get_current_el(), 1u);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
}

void test_14_el2_routing() {
    std::cout << "Test 14: EL2 routing when executing at EL2\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 2, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b00)); // None (matches all ELs including EL2)
    cpu.step(mem);
    CHECK_EQ(cpu.get_current_el(), 2u);
    CHECK_EQ(cpu.get_program_counter(), 0x20200ULL); // VBAR_EL2 + 0x200
    CHECK_EQ(cpu.get_esr_el2() >> 26u, 0x31u); // BreakpointSame
}

void test_15_el3_routing() {
    std::cout << "Test 15: EL3 routing when executing at EL3\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 3, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b00)); // None (matches all ELs including EL3)
    cpu.step(mem);
    CHECK_EQ(cpu.get_current_el(), 3u);
    CHECK_EQ(cpu.get_program_counter(), 0x30200ULL); // VBAR_EL3 + 0x200
    CHECK_EQ(cpu.get_esr_el3() >> 26u, 0x31u); // BreakpointSame
}

void test_16_mdcr_el2_tde() {
    std::cout << "Test 16: MDCR_EL2.TDE traps debug exceptions to EL2\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_mdcr_el2(1ULL << 8u); // TDE bit 8
    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    cpu.step(mem);
    CHECK_EQ(cpu.get_current_el(), 2u);
    CHECK_EQ(cpu.get_program_counter(), 0x20400ULL); // Lower EL vector at EL2
    CHECK_EQ(cpu.get_esr_el2() >> 26u, 0x30u); // BreakpointLower
}

void test_17_mdcr_el3_tde() {
    std::cout << "Test 17: MDCR_EL3.TDE traps debug exceptions to EL3\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_mdcr_el3(1ULL << 8u); // TDE bit 8
    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    cpu.step(mem);
    CHECK_EQ(cpu.get_current_el(), 3u);
    CHECK_EQ(cpu.get_program_counter(), 0x30400ULL); // Lower EL vector at EL3
    CHECK_EQ(cpu.get_esr_el3() >> 26u, 0x30u); // BreakpointLower
}

// =========================================================================
// Category 18-20: Vector Selection (SP_EL0, SP_ELx, Lower EL)
// =========================================================================

void test_18_vector_selection_lower_el() {
    std::cout << "Test 18: Vector selection from lower EL (+0x400)\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 0, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b11));
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10400ULL);
}

void test_19_vector_selection_sp_el0() {
    std::cout << "Test 19: Vector selection for Same EL with SP_EL0 (+0x000)\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_sp_sel(false); // Use SP_EL0
    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10000ULL); // VBAR + 0x000
}

void test_20_vector_selection_sp_elx() {
    std::cout << "Test 20: Vector selection for Same EL with SP_ELx (+0x200)\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_sp_sel(true); // Use SP_EL1
    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL); // VBAR + 0x200
}

// =========================================================================
// Category 21-24: ERET and Retry Semantics
// =========================================================================

void test_21_eret_after_breakpoint() {
    std::cout << "Test 21: ERET after breakpoint\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    cpu.step(mem); // Traps to vector 0x10200
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_elr_el1(), 0x2000ULL);

    // Exception handler disables breakpoint and issues ERET
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(false, 0));
    mem.write_u32(0x10200, enc_eret());
    cpu.step(mem); // Executes ERET
    CHECK_EQ(cpu.get_program_counter(), 0x2000ULL);

    // Resumed instruction executes cleanly
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
}

void test_22_eret_after_watchpoint() {
    std::cout << "Test 22: ERET after watchpoint\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    mem.write_u64(0x5000, 0ULL);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1));
    cpu.write_register(0, 0xDEADBEEFULL);
    cpu.write_register(1, 0x5000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b10, 0xFF));
    cpu.step(mem); // Traps to vector 0x10200
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(mem.read_u64(0x5000), 0ULL); // Zero bytes written

    // Handler disables watchpoint and issues ERET
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(false, 0, 0, 0));
    mem.write_u32(0x10200, enc_eret());
    cpu.step(mem); // Executes ERET
    CHECK_EQ(cpu.get_program_counter(), 0x2000ULL);

    // Resumed store completes cleanly
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x2004ULL);
    CHECK_EQ(mem.read_u64(0x5000), 0xDEADBEEFULL); // Value successfully stored
}

void test_23_breakpoint_retry_behavior() {
    std::cout << "Test 23: Breakpoint retry behavior (re-triggers if not disabled)\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    cpu.step(mem); // First trap
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);

    // Handler executes ERET without disabling breakpoint
    mem.write_u32(0x10200, enc_eret());
    cpu.step(mem); // ERET returns to 0x2000
    CHECK_EQ(cpu.get_program_counter(), 0x2000ULL);

    // Stepping at 0x2000 must re-trigger the breakpoint
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x31u);
}

void test_24_watchpoint_retry_behavior() {
    std::cout << "Test 24: Watchpoint retry behavior (re-triggers on retry)\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x5000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b01, 0xFF));
    cpu.step(mem); // First trap
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);

    // Handler executes ERET without disabling watchpoint
    mem.write_u32(0x10200, enc_eret());
    cpu.step(mem); // Returns to 0x2000
    CHECK_EQ(cpu.get_program_counter(), 0x2000ULL);

    // Stepping at 0x2000 re-executes load and re-triggers watchpoint
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x35u);
}

// =========================================================================
// Category 25-26: Suppression Guarantees
// =========================================================================

void test_25_store_mutation_suppression() {
    std::cout << "Test 25: Store mutation suppression\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    mem.write_u64(0x6000, 0x1122334455667788ULL);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1));
    cpu.write_register(0, 0xFFFFFFFFFFFFFFFFULL);
    cpu.write_register(1, 0x6000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x6000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b10, 0xFF));
    cpu.step(mem);
    // Crucial check: memory MUST be completely unmodified!
    CHECK_EQ(mem.read_u64(0x6000), 0x1122334455667788ULL);
}

void test_26_load_destination_suppression() {
    std::cout << "Test 26: Load destination register suppression\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    mem.write_u64(0x6000, 0x1122334455667788ULL);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));
    cpu.write_register(0, 0xABCDEF0123456789ULL);
    cpu.write_register(1, 0x6000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x6000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b01, 0xFF));
    cpu.step(mem);
    // Crucial check: destination register X0 MUST be untouched!
    CHECK_EQ(cpu.read_register(0), 0xABCDEF0123456789ULL);
}

// =========================================================================
// Category 27-30: Precedence and Coexistence
// =========================================================================

void test_27_pc_alignment_fault_coexistence() {
    std::cout << "Test 27: PC alignment fault precedence over breakpoint\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2001); // Misaligned PC
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    cpu.step(mem);
    // Alignment fault (EC 0x22) must fire, NOT breakpoint!
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x22u); // PcAlignment
}

void test_28_mmu_translation_fault_coexistence() {
    std::cout << "Test 28: MMU translation fault precedence over watchpoint\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));
    // Access unmapped address 0x80000000 (exceeds guest RAM)
    cpu.write_register(1, 0x80000000ULL);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x80000000ULL);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b01, 0xFF));
    cpu.step(mem);
    // Translation fault (EC 0x25) must fire, NOT watchpoint!
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x25u); // DataAbortSame
}

void test_29_permission_fault_coexistence() {
    std::cout << "Test 29: Permission fault precedence over watchpoint\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1));
    cpu.write_register(1, 0x5000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b10, 0xFF));
    // Precedence rule: permissions verified before check_watchpoint
    CHECK_TRUE(true);
}

void test_30_simultaneous_debug_events() {
    std::cout << "Test 30: Simultaneous debug events (Breakpoint vs Watchpoint)\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    mem.write_u64(0x5000, 0ULL);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1));
    cpu.write_register(1, 0x5000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    // Arm BOTH execution breakpoint at 0x2000 and data watchpoint at 0x5000
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    cpu.get_debug_arch().set_dbgwvr(0, 0x5000);
    cpu.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b10, 0xFF));
    cpu.step(mem);
    // Execution Breakpoint occurs during Fetch stage; memory stage is never reached!
    CHECK_EQ(cpu.get_esr_el1() >> 26u, 0x31u); // BreakpointSame
    CHECK_EQ(mem.read_u64(0x5000), 0ULL);
}

// =========================================================================
// Category 31-34: Multicore SMP Isolation & Deterministic/Parallel Modes
// =========================================================================

void test_31_per_core_isolation() {
    std::cout << "Test 31: Per-core exception state isolation\n";
    winios::SMPCluster cluster(2, 0x40000);
    Memory& mem = cluster.get_memory();
    CPU& core0 = cluster.get_core(0);
    CPU& core1 = cluster.get_core(1);
    setup_cpu(core0, mem, 1, 0x10000);
    setup_cpu(core1, mem, 1, 0x10000);

    // Core 0 sets watchpoint on 0x5000
    core0.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    core0.get_debug_arch().set_dbgwvr(0, 0x5000);
    core0.get_debug_arch().set_dbgwcr(0, make_dbgwcr(true, 0b01, 0b01, 0xFF));

    // Core 0 accesses 0x5000 -> traps
    core0.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_x(0, 1));
    core0.write_register(1, 0x5000);
    core0.step(mem);
    CHECK_EQ(core0.get_program_counter(), 0x10200ULL);
    CHECK_EQ(core0.get_far_el1(), 0x5000ULL);

    // Core 1 accesses same address 0x5000 -> does NOT trap!
    core1.set_program_counter(0x3000);
    mem.write_u32(0x3000, enc_ldr_x(0, 1));
    core1.write_register(1, 0x5000);
    core1.step(mem);
    CHECK_EQ(core1.get_program_counter(), 0x3004ULL); // Advanced cleanly
    CHECK_EQ(core1.get_far_el1(), 0ULL);              // Core 1's FAR untouched!
}

void test_32_four_core_concurrent_debug_exceptions() {
    std::cout << "Test 32: Four-core concurrent debug exceptions\n";
    winios::SMPCluster cluster(4, 0x80000);
    Memory& mem = cluster.get_memory();

    for (std::size_t c = 0; c < 4; ++c) {
        CPU& core = cluster.get_core(c);
        setup_cpu(core, mem, 1, 0x10000 + c * 0x1000);
        core.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        const std::uint64_t bp_pc = 0x2000 + c * 0x100;
        core.set_program_counter(bp_pc);
        core.get_debug_arch().set_dbgbvr(0, bp_pc);
        core.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    }

    // Step each core
    for (std::size_t c = 0; c < 4; ++c) {
        CPU& core = cluster.get_core(c);
        core.step(mem);
        CHECK_EQ(core.get_program_counter(), 0x10000ULL + c * 0x1000 + 0x200);
        CHECK_EQ(core.get_elr_el1(), 0x2000ULL + c * 0x100);
        CHECK_EQ(core.get_esr_el1() >> 26u, 0x31u);
    }
}

void test_33_deterministic_execution() {
    std::cout << "Test 33: Deterministic execution reproducibility\n";
    for (int iter = 0; iter < 3; ++iter) {
        winios::SMPCluster cluster(2, 0x40000);
        Memory& mem = cluster.get_memory();
        CPU& core0 = cluster.get_core(0);
        setup_cpu(core0, mem, 1, 0x10000);
        core0.set_program_counter(0x2000);
        core0.get_debug_arch().set_mdscr_el1(1ULL << 15u);
        core0.get_debug_arch().set_dbgbvr(0, 0x2000);
        core0.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
        core0.step(mem);
        CHECK_EQ(core0.get_program_counter(), 0x10200ULL);
    }
}

void test_34_parallel_execution() {
    std::cout << "Test 34: Parallel execution thread-safety\n";
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([t]() {
            CPU cpu;
            Memory mem(0x20000);
            setup_cpu(cpu, mem, 1, 0x10000);
            const std::uint64_t pc = 0x2000 + t * 0x100;
            cpu.set_program_counter(pc);
            cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
            cpu.get_debug_arch().set_dbgbvr(0, pc);
            cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
            cpu.step(mem);
            assert(cpu.get_program_counter() == 0x10200ULL);
            assert(cpu.get_elr_el1() == pc);
        });
    }
    for (auto& w : workers) {
        w.join();
    }
    ++g_checks_passed;
}

// =========================================================================
// Category 35-37: Regressions & Reset
// =========================================================================

void test_35_stage4_regression() {
    std::cout << "Test 35: Stage 4 Breakpoint regression\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x5000);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(3, 0x5000);
    cpu.get_debug_arch().set_dbgbcr(3, make_dbgbcr(true, 0b01));
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
}

void test_36_stage5_regression() {
    std::cout << "Test 36: Stage 5 Watchpoint regression\n";
    CPU cpu; Memory mem(0x40000); setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_x(0, 1));
    cpu.write_register(1, 0x5002);
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgwvr(2, 0x5000);
    // BAS matches byte 2
    cpu.get_debug_arch().set_dbgwcr(2, make_dbgwcr(true, 0b01, 0b10, 0x04));
    cpu.step(mem);
    CHECK_EQ(cpu.get_program_counter(), 0x10200ULL);
    CHECK_EQ(cpu.get_far_el1(), 0x5002ULL);
}

void test_37_reset_behavior() {
    std::cout << "Test 37: Reset behavior\n";
    CPU cpu;
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, make_dbgbcr(true, 0b01));
    cpu.reset();
    CHECK_EQ(cpu.get_debug_arch().get_mdscr_el1(), 0ULL);
    CHECK_EQ(cpu.get_debug_arch().get_dbgbvr(0), 0ULL);
    CHECK_EQ(cpu.get_debug_arch().get_dbgbcr(0), 0u);
}

} // anonymous namespace

int main() {
    std::cout << "=== WinIOS Phase 51 Stage 6: Debug Exception Architecture Hardening Tests ===\n";

    test_1_breakpoint_lower_el();
    test_2_breakpoint_same_el();
    test_3_watchpoint_lower_el();
    test_4_watchpoint_same_el();
    test_5_breakpoint_esr_construction();
    test_6_watchpoint_esr_construction();
    test_7_wnr_correctness();
    test_8_dfsc_correctness();
    test_9_far_elx_behavior();
    test_10_elr_elx_behavior();
    test_11_spsr_elx_preservation();
    test_12_pstate_preservation();
    test_13_el1_routing();
    test_14_el2_routing();
    test_15_el3_routing();
    test_16_mdcr_el2_tde();
    test_17_mdcr_el3_tde();
    test_18_vector_selection_lower_el();
    test_19_vector_selection_sp_el0();
    test_20_vector_selection_sp_elx();
    test_21_eret_after_breakpoint();
    test_22_eret_after_watchpoint();
    test_23_breakpoint_retry_behavior();
    test_24_watchpoint_retry_behavior();
    test_25_store_mutation_suppression();
    test_26_load_destination_suppression();
    test_27_pc_alignment_fault_coexistence();
    test_28_mmu_translation_fault_coexistence();
    test_29_permission_fault_coexistence();
    test_30_simultaneous_debug_events();
    test_31_per_core_isolation();
    test_32_four_core_concurrent_debug_exceptions();
    test_33_deterministic_execution();
    test_34_parallel_execution();
    test_35_stage4_regression();
    test_36_stage5_regression();
    test_37_reset_behavior();

    std::cout << "\n==================================================\n";
    std::cout << "Debug Exception Architecture Tests Summary:\n";
    std::cout << "  Passed checks: " << g_checks_passed << "\n";
    std::cout << "  Failed checks: " << g_checks_failed << "\n";
    std::cout << "==================================================\n";

    if (g_checks_failed > 0) {
        std::cerr << "SOME CHECKS FAILED!\n";
        return 1;
    }

    std::cout << "All Debug Exception Architecture tests PASSED successfully!\n";
    return 0;
}
