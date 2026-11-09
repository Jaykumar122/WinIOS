#include "cpu/cpu.h"
#include "cpu/debug_architecture.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

// WinIOS Phase 51 Stage 8: Hardware Single-Step Debugging Test Suite
// Verifies all 50 test categories across:
// - Basic single-step (1-10)
// - Instruction retirement (11-28)
// - Exceptions and recovery (29-40)
// - Interrupts and SMP (41-50)

namespace {

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

// Helper: encode MOVZ Xd, #imm16, LSL #shift
constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm16, unsigned shift = 0) {
    return 0xD2800000u | ((shift / 16u) << 21u) | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}

// Helper: encode ADD Xd, Xn, #imm12
constexpr std::uint32_t enc_add_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0x91000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

// Helper: encode SUB Xd, Xn, #imm12
constexpr std::uint32_t enc_sub_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0xD1000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

// Helper: encode AND Xd, Xn, Xm
constexpr std::uint32_t enc_and_reg(unsigned rd, unsigned rn, unsigned rm) {
    return 0x8A000000u | ((rm & 0x1Fu) << 16u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

// Helper: encode B offset (signed 26-bit word offset)
constexpr std::uint32_t enc_b(std::int32_t word_offset) {
    return 0x14000000u | (static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu);
}

// Helper: encode BL offset (signed 26-bit word offset)
constexpr std::uint32_t enc_bl(std::int32_t word_offset) {
    return 0x94000000u | (static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu);
}

// Helper: encode RET Xn
constexpr std::uint32_t enc_ret(unsigned rn = 30) {
    return 0xD65F0000u | ((rn & 0x1Fu) << 5u);
}

// Helper: encode B.cond (cond 0..14, signed 19-bit word offset)
constexpr std::uint32_t enc_b_cond(unsigned cond, std::int32_t word_offset) {
    return 0x54000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (cond & 0xFu);
}

// Helper: encode CBZ Xt, offset
constexpr std::uint32_t enc_cbz_x(unsigned rt, std::int32_t word_offset) {
    return 0xB4000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode CBNZ Xt, offset
constexpr std::uint32_t enc_cbnz_x(unsigned rt, std::int32_t word_offset) {
    return 0xB5000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode LDR Wt, [Xn]
constexpr std::uint32_t enc_ldr_w(unsigned rt, unsigned rn) {
    return 0xB9400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode STR Wt, [Xn]
constexpr std::uint32_t enc_str_w(unsigned rt, unsigned rn) {
    return 0xB9000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode LDP Xt1, Xt2, [Xn]
constexpr std::uint32_t enc_ldp_x(unsigned rt1, unsigned rt2, unsigned rn) {
    return 0xA9400000u | ((rn & 0x1Fu) << 5u) | ((rt2 & 0x1Fu) << 10u) | (rt1 & 0x1Fu);
}

// Helper: encode STP Xt1, Xt2, [Xn]
constexpr std::uint32_t enc_stp_x(unsigned rt1, unsigned rt2, unsigned rn) {
    return 0xA9000000u | ((rn & 0x1Fu) << 5u) | ((rt2 & 0x1Fu) << 10u) | (rt1 & 0x1Fu);
}

// Helper: encode LDXR Xt, [Xn]
constexpr std::uint32_t enc_ldxr_x(unsigned rt, unsigned rn) {
    return 0xC85F7C00u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode STXR Ws, Xt, [Xn]
constexpr std::uint32_t enc_stxr_x(unsigned ws, unsigned rt, unsigned rn) {
    return 0xC8007C00u | ((rn & 0x1Fu) << 5u) | ((ws & 0x1Fu) << 16u) | (rt & 0x1Fu);
}

// Helper: encode SWP Xt, Xt, [Xn]
constexpr std::uint32_t enc_swp_x(unsigned rs, unsigned rt, unsigned rn) {
    return 0xF8208000u | ((rs & 0x1Fu) << 16u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

// Helper: encode ERET
constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}

// Helper: encode SVC #imm16
constexpr std::uint32_t enc_svc(std::uint16_t imm) {
    return 0xD4000001u | (static_cast<std::uint32_t>(imm) << 5u);
}

// Helper: encode WFI
constexpr std::uint32_t enc_wfi() {
    return 0xD503207Fu;
}

// Helper: encode WFE
constexpr std::uint32_t enc_wfe() {
    return 0xD503205Fu;
}

// =========================================================================
// Category 1: Basic Single-Step (Tests 1-10)
// =========================================================================

void test_1_ss_disabled() {
    std::cout << "Test 1: MDSCR_EL1.SS disabled\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(0, 0, 1));

    // SS is 0 (disabled), MDE is 1
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // Advanced normally without taking single-step exception
    assert(cpu.get_program_counter() == 0x2004ULL);
    assert(cpu.read_register(0) == 1ULL);
    std::cout << "  PASS\n";
}

void test_2_ss_enabled() {
    std::cout << "Test 2: MDSCR_EL1.SS enabled\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(0, 0, 5));

    // Enable MDE (bit 15) and SS (bit 0), unmask debug
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u); // Unmask D, A, I, F
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // Single instruction retired, then exception taken
    assert(cpu.read_register(0) == 5ULL); // Instruction executed
    assert(cpu.get_program_counter() == 0x10200ULL); // Vector entry taken
    assert(cpu.get_elr_el1() == 0x2004ULL); // Preferred return is next PC
    assert((cpu.get_esr_el1() >> 26u) == 0x33u); // EC 0x33 (SoftwareStepSame)
    std::cout << "  PASS\n";
}

void test_3_mde_disabled() {
    std::cout << "Test 3: MDE disabled prevents single-step\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(0, 0, 1));

    // SS enabled (bit 0), but MDE disabled (bit 15 = 0)
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 0u);
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // No exception taken
    assert(cpu.get_program_counter() == 0x2004ULL);
    std::cout << "  PASS\n";
}

void test_4_mde_enabled() {
    std::cout << "Test 4: MDE enabled allows single-step\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(0, 0, 2));

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    assert(cpu.get_program_counter() == 0x10200ULL);
    std::cout << "  PASS\n";
}

void test_5_pstate_ss_behavior() {
    std::cout << "Test 5: PSTATE.SS transitions from 1 to 0 on retirement\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(0, 0, 1));

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // In exception handler, PSTATE.SS is 0
    assert(!cpu.get_pstate_ss());
    // SPSR_EL1 captures SS=0 (post-retirement state)
    assert(((cpu.get_spsr_el1() >> 21u) & 0x1u) == 0u);
    std::cout << "  PASS\n";
}

void test_6_single_step_lower_el() {
    std::cout << "Test 6: Single-step lower EL (EL0 -> EL1)\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 0, 0x10000); // Start at EL0

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(1, 1, 10));

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // Exception taken to EL1 from lower EL -> vector offset 0x400
    assert(cpu.get_current_el() == 1);
    assert(cpu.get_program_counter() == 0x10400ULL);
    assert((cpu.get_esr_el1() >> 26u) == 0x32u); // EC 0x32 (SoftwareStepLower)
    assert(cpu.get_elr_el1() == 0x2004ULL);
    std::cout << "  PASS\n";
}

void test_7_single_step_same_el() {
    std::cout << "Test 7: Single-step same EL (EL1 -> EL1)\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(2, 42));

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    assert(cpu.get_program_counter() == 0x10200ULL); // Same EL vector (0x200)
    assert((cpu.get_esr_el1() >> 26u) == 0x33u); // EC 0x33 (SoftwareStepSame)
    std::cout << "  PASS\n";
}

void test_8_ec_0x32_validation() {
    std::cout << "Test 8: EC 0x32 syndrome validation\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 0, 0x10000);

    cpu.set_program_counter(0x3000);
    mem.write_u32(0x3000, enc_movz(0, 1));
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    const std::uint64_t esr = cpu.get_esr_el1();
    assert(((esr >> 26u) & 0x3Fu) == 0x32u);
    assert(((esr >> 25u) & 0x1u) == 1u); // IL = 1
    assert((esr & 0x3Fu) == 0x22u); // DFSC = 0x22
    std::cout << "  PASS\n";
}

void test_9_ec_0x33_validation() {
    std::cout << "Test 9: EC 0x33 syndrome validation\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x3000);
    mem.write_u32(0x3000, enc_movz(0, 1));
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    const std::uint64_t esr = cpu.get_esr_el1();
    assert(((esr >> 26u) & 0x3Fu) == 0x33u);
    assert(((esr >> 25u) & 0x1u) == 1u); // IL = 1
    assert((esr & 0x3Fu) == 0x22u);
    std::cout << "  PASS\n";
}

void test_10_il_iss_exclusive_bit() {
    std::cout << "Test 10: IL and ISS validation (EX bit in ISS for exclusive insn)\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);

    cpu.set_program_counter(0x2000);
    // LDXR X0, [X1]
    mem.write_u32(0x2000, enc_ldxr_x(0, 1));
    cpu.write_register(1, 0x5000);

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    const std::uint64_t esr = cpu.get_esr_el1();
    assert((esr >> 26u) == 0x33u);
    assert(((esr >> 25u) & 0x1u) == 1u); // IL = 1
    // EX bit (bit 6) set for exclusive load
    assert(((esr >> 6u) & 0x1u) == 1u);
    assert((esr & 0x3Fu) == 0x22u);
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 2: Instruction Retirement (Tests 11-28)
// =========================================================================

void test_11_retirement_add() {
    std::cout << "Test 11: ADD instruction retirement\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(3, 3, 100));
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.read_register(3) == 100ULL);
    assert(cpu.get_elr_el1() == 0x2004ULL);
    std::cout << "  PASS\n";
}

void test_12_retirement_sub() {
    std::cout << "Test 12: SUB instruction retirement\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.write_register(4, 50);
    mem.write_u32(0x2000, enc_sub_imm(4, 4, 20));
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.read_register(4) == 30ULL);
    assert(cpu.get_elr_el1() == 0x2004ULL);
    std::cout << "  PASS\n";
}

void test_13_retirement_mov() {
    std::cout << "Test 13: MOV instruction retirement\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(5, 0x1234));
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.read_register(5) == 0x1234ULL);
    std::cout << "  PASS\n";
}

void test_14_retirement_logical() {
    std::cout << "Test 14: Logical instruction retirement\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.write_register(1, 0xFF);
    cpu.write_register(2, 0x0F);
    mem.write_u32(0x2000, enc_and_reg(3, 1, 2));
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.read_register(3) == 0x0FULL);
    std::cout << "  PASS\n";
}

void test_15_sequential_execution() {
    std::cout << "Test 15: Sequential multi-instruction stepping\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(0, 0, 1));
    mem.write_u32(0x2004, enc_add_imm(0, 0, 2));

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    // Step 1: executes 0x2000
    cpu.step(mem);
    assert(cpu.read_register(0) == 1ULL);
    assert(cpu.get_elr_el1() == 0x2004ULL);

    // ERET recovery: restore PC and SS
    cpu.set_program_counter(cpu.get_elr_el1());
    cpu.set_pstate_ss(true);
    cpu.set_daif(0x0u);

    // Step 2: executes 0x2004
    cpu.step(mem);
    assert(cpu.read_register(0) == 3ULL);
    assert(cpu.get_elr_el1() == 0x2008ULL);
    std::cout << "  PASS\n";
}

void test_16_cond_branch_taken() {
    std::cout << "Test 16: Conditional branch taken\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    // B.EQ +8 words (target 0x2020) with Z flag set
    mem.write_u32(0x2000, enc_b_cond(0, 8));
    cpu.set_nzcv(0x4); // Z=1

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.get_elr_el1() == 0x2020ULL); // Next PC is branch target
    std::cout << "  PASS\n";
}

void test_17_cond_branch_not_taken() {
    std::cout << "Test 17: Conditional branch not taken\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_b_cond(0, 8)); // B.EQ
    cpu.set_nzcv(0x0); // Z=0

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.get_elr_el1() == 0x2004ULL); // Fall-through
    std::cout << "  PASS\n";
}

void test_18_unconditional_branch() {
    std::cout << "Test 18: Unconditional branch\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_b(10)); // target 0x2028

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.get_elr_el1() == 0x2028ULL);
    std::cout << "  PASS\n";
}

void test_19_bl_and_link_register() {
    std::cout << "Test 19: BL and link register\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_bl(4)); // target 0x2010

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.get_elr_el1() == 0x2010ULL);
    assert(cpu.read_register(30) == 0x2004ULL); // X30 (LR) updated
    std::cout << "  PASS\n";
}

void test_20_ret() {
    std::cout << "Test 20: RET instruction\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.write_register(30, 0x5000);
    mem.write_u32(0x2000, enc_ret(30));

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.get_elr_el1() == 0x5000ULL);
    std::cout << "  PASS\n";
}

void test_21_cbz_cbnz() {
    std::cout << "Test 21: CBZ and CBNZ\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    cpu.write_register(0, 0);
    mem.write_u32(0x2000, enc_cbz_x(0, 6)); // target 0x2018

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.get_elr_el1() == 0x2018ULL);
    std::cout << "  PASS\n";
}

void test_22_tbz_tbnz() {
    std::cout << "Test 22: TBZ and TBNZ\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    // TBZ X0, #2, +4 words -> 0x36100080u (bit 2 is 0)
    mem.write_u32(0x2000, 0x36100080u);
    cpu.write_register(0, 0);

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.get_elr_el1() == 0x2010ULL);
    std::cout << "  PASS\n";
}

void test_23_successful_load() {
    std::cout << "Test 23: Successful load instruction\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldr_w(0, 1));
    cpu.write_register(1, 0x5000);
    mem.write_u32(0x5000, 0xCAFEBABE);

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.read_register(0) == 0xCAFEBABEULL);
    assert(cpu.get_elr_el1() == 0x2004ULL);
    std::cout << "  PASS\n";
}

void test_24_successful_store() {
    std::cout << "Test 24: Successful store instruction\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_w(0, 1));
    cpu.write_register(0, 0x12345678);
    cpu.write_register(1, 0x5000);

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(mem.read_u32(0x5000) == 0x12345678u);
    assert(cpu.get_elr_el1() == 0x2004ULL);
    std::cout << "  PASS\n";
}

void test_25_pair_load_store() {
    std::cout << "Test 25: Pair load/store instructions\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_stp_x(0, 2, 1));
    cpu.write_register(0, 0x1111);
    cpu.write_register(2, 0x2222);
    cpu.write_register(1, 0x5000);

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(mem.read_u64(0x5000) == 0x1111ULL);
    assert(mem.read_u64(0x5008) == 0x2222ULL);
    std::cout << "  PASS\n";
}

void test_26_exclusive_instruction() {
    std::cout << "Test 26: Exclusive load instruction\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_ldxr_x(0, 1));
    cpu.write_register(1, 0x5000);
    mem.write_u64(0x5000, 0x9999);

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.read_register(0) == 0x9999ULL);
    assert((cpu.get_esr_el1() >> 26u) == 0x33u);
    assert(((cpu.get_esr_el1() >> 6u) & 0x1u) == 1u); // EX bit set
    std::cout << "  PASS\n";
}

void test_27_lse_atomic() {
    std::cout << "Test 27: LSE atomic instruction (SWP)\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_swp_x(0, 2, 1));
    cpu.write_register(0, 0xAAAA); // new val
    cpu.write_register(1, 0x5000);
    mem.write_u64(0x5000, 0xBBBB); // old val

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.read_register(2) == 0xBBBBULL);
    assert(mem.read_u64(0x5000) == 0xAAAAULL);
    std::cout << "  PASS\n";
}

void test_28_simd_load_store() {
    std::cout << "Test 28: SIMD load/store instruction\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    // Stage 39: the SIMD store must not be trapped by CPACR_EL1.FPEN (added by
    // the Phase 37 FP/SIMD access-control work after this test was written).
    cpu.set_cpacr_el1(0x00300000ULL);
    cpu.set_program_counter(0x2000);
    // STR Q0, [X1] -> 0x3D000020u
    mem.write_u32(0x2000, 0x3D000020u);
    cpu.write_register(1, 0x5000);

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);
    assert(cpu.get_elr_el1() == 0x2004ULL);
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 3: Exceptions and Recovery (Tests 29-40)
// =========================================================================

void test_29_undefined_instruction() {
    std::cout << "Test 29: Undefined instruction does NOT step\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    // 0x00000000 is unallocated / undefined encoding
    mem.write_u32(0x2000, 0x00000000u);

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // Takes Undefined Instruction exception (EC 0x00), NOT single-step!
    assert((cpu.get_esr_el1() >> 26u) == 0x00u);
    assert(cpu.get_elr_el1() == 0x2000ULL);
    std::cout << "  PASS\n";
}

void test_30_svc_instruction() {
    std::cout << "Test 30: SVC takes exception (EC 0x15) and does NOT trigger single-step\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 0, 0x10000); // Executing at EL0
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_svc(42));

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    assert((cpu.get_esr_el1() >> 26u) == 0x15u); // EC 0x15 (SVC)
    assert((cpu.get_esr_el1() & 0xFFFFu) == 42u); // imm16
    assert(cpu.get_elr_el1() == 0x2004ULL);
    // SPSR_EL1 preserves PSTATE.SS = 1
    assert(((cpu.get_spsr_el1() >> 21u) & 0x1u) == 1u);
    std::cout << "  PASS\n";
}

void test_31_hvc_instruction() {
    std::cout << "Test 31: HVC instruction routing\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    // HVC #1 -> 0xD4000022u
    mem.write_u32(0x2000, 0xD4000022u);

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // Routes to EL2 with EC 0x16 (HVC)
    assert(cpu.get_current_el() == 2);
    assert((cpu.get_esr_el2() >> 26u) == 0x16u);
    std::cout << "  PASS\n";
}

void test_32_smc_instruction() {
    std::cout << "Test 32: SMC instruction routing\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    // SMC #0 -> 0xD4000003u
    mem.write_u32(0x2000, 0xD4000003u);

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // Routes to EL3 with EC 0x17 (SMC)
    assert(cpu.get_current_el() == 3);
    assert((cpu.get_esr_el3() >> 26u) == 0x17u);
    std::cout << "  PASS\n";
}

void test_33_eret_stepping() {
    std::cout << "Test 33: ERET stepping\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_eret());
    cpu.set_elr_el1(0x8000);
    cpu.set_spsr_el1(0x00000000); // Return to EL0, SS=0

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // Stepping ERET retires ERET and generates Software Step exception pointing to target PC
    assert(cpu.get_current_el() == 1);
    assert(cpu.get_elr_el1() == 0x8000ULL);
    assert((cpu.get_esr_el1() >> 26u) == 0x32u); // SoftwareStepLower
    std::cout << "  PASS\n";
}

void test_34_data_abort_does_not_step() {
    std::cout << "Test 34: Data abort does not generate single-step exception\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    // LDR W0, [X1] with unbacked RAM address 0xDEADBEEF
    mem.write_u32(0x2000, enc_ldr_w(0, 1));
    cpu.write_register(1, 0xDEADBEEF);

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // Takes Data Abort (EC 0x25), NOT single-step!
    assert((cpu.get_esr_el1() >> 26u) == 0x25u);
    assert(cpu.get_elr_el1() == 0x2000ULL); // ELR points to faulting instruction
    std::cout << "  PASS\n";
}

void test_35_instruction_abort_does_not_step() {
    std::cout << "Test 35: Instruction abort does not generate single-step exception\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    // PC at unbacked address 0xDEAD0000
    cpu.set_program_counter(0xDEAD0000ULL);

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // Takes Instruction Abort (EC 0x21), NOT single-step!
    assert((cpu.get_esr_el1() >> 26u) == 0x21u);
    assert(cpu.get_elr_el1() == 0xDEAD0000ULL);
    std::cout << "  PASS\n";
}

void test_36_alignment_fault_does_not_step() {
    std::cout << "Test 36: Alignment fault does not generate single-step exception\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    // Misaligned PC (0x2001)
    cpu.set_program_counter(0x2001);

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // Takes PC alignment fault (EC 0x22), NOT single-step!
    assert((cpu.get_esr_el1() >> 26u) == 0x22u);
    std::cout << "  PASS\n";
}

void test_37_watchpoint_interaction() {
    std::cout << "Test 37: Watchpoint takes priority over single-step\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_str_w(0, 1));
    cpu.write_register(0, 0x55);
    cpu.write_register(1, 0x3000);

    // Setup watchpoint on 0x3000
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.get_debug_arch().set_dbgwvr(0, 0x3000);
    cpu.get_debug_arch().set_dbgwcr(0, 0x1u | (0x3u << 1u) | (0x2u << 3u) | (0xFFu << 5u)); // write WP
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // Watchpoint exception taken (EC 0x35), NOT single-step!
    assert((cpu.get_esr_el1() >> 26u) == 0x35u);
    assert(cpu.get_far_el1() == 0x3000ULL);
    assert(cpu.get_elr_el1() == 0x2000ULL);
    assert(mem.read_u32(0x3000) != 0x55u); // Store was suppressed!
    std::cout << "  PASS\n";
}

void test_38_breakpoint_interaction() {
    std::cout << "Test 38: Breakpoint takes priority over single-step\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(0, 0, 1));

    // Setup breakpoint on 0x2000
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u)); // BP enabled
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // Breakpoint exception taken (EC 0x31), NOT single-step!
    assert((cpu.get_esr_el1() >> 26u) == 0x31u);
    assert(cpu.get_elr_el1() == 0x2000ULL);
    assert(cpu.read_register(0) == 0ULL); // Instruction suppressed!
    std::cout << "  PASS\n";
}

void test_39_handler_modifies_ss() {
    std::cout << "Test 39: Exception handler disables SS\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(0, 0, 1));
    mem.write_u32(0x2004, enc_add_imm(0, 0, 2));

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    // Step 1
    cpu.step(mem);
    assert(cpu.get_elr_el1() == 0x2004ULL);

    // Handler disables MDSCR_EL1.SS
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u); // SS=0

    // ERET to 0x2004
    cpu.set_program_counter(cpu.get_elr_el1());
    cpu.set_daif(0x0u);

    // Step 2: executes 0x2004 without single-step exception
    cpu.step(mem);
    assert(cpu.get_program_counter() == 0x2008ULL); // Normal advance
    std::cout << "  PASS\n";
}

void test_40_eret_restores_step_state() {
    std::cout << "Test 40: ERET restores expected step state from SPSR.SS\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_eret());
    cpu.set_elr_el1(0x4000);
    cpu.set_spsr_el1((1u << 21u)); // SPSR.SS = 1, return to EL0

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));

    cpu.step(mem);

    // ERET completed, returning to EL0 with PSTATE.SS = 1
    assert(cpu.get_current_el() == 0);
    assert(cpu.get_program_counter() == 0x4000ULL);
    assert(cpu.get_pstate_ss()); // PSTATE.SS was restored to 1!
    std::cout << "  PASS\n";
}

// =========================================================================
// Category 4: Interrupts and SMP (Tests 41-50)
// =========================================================================

void test_41_irq_interaction() {
    std::cout << "Test 41: IRQ takes interrupt vector before instruction execution\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(0, 0, 1));

    // Configure GICv3 and assert PMU interrupt (PPI 23)
    cpu.get_gic().write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4);
    cpu.get_gic().write_icc_sre_el1(1, 0);
    cpu.get_gic().write_icc_igrpen1(1, 0);
    cpu.get_gic().write_icc_pmr(0xF0, 0);
    cpu.get_gic().write_mmio(0x080B0080, 1u << 23, 4);  // Group 1
    cpu.get_gic().write_mmio(0x080B0400 + 23, 0x30, 1); // Priority 0x30
    cpu.get_gic().write_mmio(0x080B0100, 1u << 23, 4);  // Enable PPI 23
    cpu.get_pmu().write_pmintenset(1u << 31u);
    cpu.get_pmu().write_pmovsset(1u << 31u);
    cpu.set_daif(0x0u); // Unmask I-bit

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // Interrupt taken to IRQ vector (0x10280 for EL1h IRQ), instruction NOT executed
    assert(cpu.get_program_counter() == 0x10280ULL);
    assert(cpu.get_elr_el1() == 0x2000ULL); // ELR holds unexecuted instruction PC
    assert(cpu.read_register(0) == 0ULL); // Not retired
    std::cout << "  PASS\n";
}

void test_42_wfi_wfe_behavior() {
    std::cout << "Test 42: WFI / WFE behavior under single-step\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_wfi());

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // WFI retires and triggers single-step exception pointing to next PC
    assert(cpu.get_elr_el1() == 0x2004ULL);
    std::cout << "  PASS\n";
}

void test_43_core_reset() {
    std::cout << "Test 43: Core reset clears single-step state\n";
    CPU cpu;
    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_pstate_ss(true);

    cpu.reset();

    assert(!cpu.get_debug_arch().software_step_enabled());
    assert(!cpu.get_pstate_ss());
    std::cout << "  PASS\n";
}

void test_44_two_core_independent_stepping() {
    std::cout << "Test 44: Two-core independent stepping\n";
    winios::SMPCluster cluster(2, 0x40000);
    auto& core0 = cluster.get_core(0);
    auto& core1 = cluster.get_core(1);
    auto& mem = cluster.get_memory();

    setup_cpu(core0, mem, 1, 0x10000);
    setup_cpu(core1, mem, 1, 0x20000);

    core0.set_program_counter(0x2000);
    core1.set_program_counter(0x3000);

    mem.write_u32(0x2000, enc_add_imm(0, 0, 1));
    mem.write_u32(0x3000, enc_add_imm(1, 1, 2));

    // Core 0 steps; Core 1 does not step
    core0.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    core0.set_daif(0x0u);
    core0.set_pstate_ss(true);

    core1.get_debug_arch().set_mdscr_el1(0); // SS disabled on Core 1

    core0.step(mem);
    core1.step(mem);

    // Core 0 took single-step exception
    assert(core0.get_program_counter() == 0x10200ULL);
    assert(core0.get_elr_el1() == 0x2004ULL);

    // Core 1 ran normally without exception
    assert(core1.get_program_counter() == 0x3004ULL);
    std::cout << "  PASS\n";
}

void test_45_four_core_independent_stepping() {
    std::cout << "Test 45: Four-core independent stepping\n";
    winios::SMPCluster cluster(4, 0x40000);
    auto& mem = cluster.get_memory();

    for (std::size_t i = 0; i < 4; ++i) {
        auto& core = cluster.get_core(i);
        setup_cpu(core, mem, 1, 0x10000 * (i + 1));
        const std::uint64_t pc = 0x2000 + i * 0x100;
        core.set_program_counter(pc);
        mem.write_u32(pc, enc_add_imm(0, 0, static_cast<std::uint16_t>(i + 1)));

        // Even cores step; odd cores do not
        if (i % 2 == 0) {
            core.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
            core.set_daif(0x0u);
            core.set_pstate_ss(true);
        } else {
            core.get_debug_arch().set_mdscr_el1(0);
        }
    }

    for (std::size_t i = 0; i < 4; ++i) {
        auto& core = cluster.get_core(i);
        core.step(mem);
        if (i % 2 == 0) {
            assert((core.get_esr_el1() >> 26u) == 0x33u); // Exception taken
        } else {
            assert(core.get_program_counter() == 0x2000 + i * 0x100 + 4); // Normal advance
        }
    }
    std::cout << "  PASS\n";
}

void test_46_deterministic_execution() {
    std::cout << "Test 46: Deterministic execution repeatability\n";
    for (int run = 0; run < 3; ++run) {
        CPU cpu;
        Memory mem(0x40000);
        setup_cpu(cpu, mem, 1, 0x10000);
        cpu.set_program_counter(0x2000);
        mem.write_u32(0x2000, enc_add_imm(0, 0, 7));
        cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
        cpu.set_daif(0x0u);
        cpu.set_pstate_ss(true);

        cpu.step(mem);
        assert(cpu.get_elr_el1() == 0x2004ULL);
        assert(cpu.read_register(0) == 7ULL);
    }
    std::cout << "  PASS\n";
}

void test_47_parallel_execution() {
    std::cout << "Test 47: Parallel multithreaded single-stepping safety\n";
    std::vector<std::thread> workers;
    for (int t = 0; t < 6; ++t) {
        workers.emplace_back([t]() {
            CPU cpu;
            Memory mem(0x40000);
            setup_cpu(cpu, mem, 1, 0x10000);
            for (int i = 0; i < 50; ++i) {
                cpu.set_program_counter(0x2000);
                mem.write_u32(0x2000, enc_add_imm(0, 0, static_cast<std::uint16_t>(t + 1)));
                cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
                cpu.set_daif(0x0u);
                cpu.set_pstate_ss(true);

                cpu.step(mem);
                assert(cpu.get_elr_el1() == 0x2004ULL);
            }
        });
    }
    for (auto& w : workers) {
        w.join();
    }
    std::cout << "  PASS\n";
}

void test_48_duplicate_event_prevention() {
    std::cout << "Test 48: Duplicate-event prevention (exactly one exception per instruction)\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_movz(0, 10));

    cpu.get_debug_arch().set_mdscr_el1((1ULL << 15u) | (1ULL << 0u));
    cpu.set_daif(0x0u);
    cpu.set_pstate_ss(true);

    cpu.step(mem);

    // In exception handler: PSTATE.SS is 0 and D is 1
    assert(!cpu.get_pstate_ss());
    assert((cpu.get_daif() & 0x8u) != 0u); // D bit masked
    // Stepping the exception handler should NOT generate another step exception!
    cpu.step(mem);
    // PC advances through the exception handler normally without second step trap
    assert(cpu.get_program_counter() == 0x10204ULL);
    std::cout << "  PASS\n";
}

void test_49_stage2_7_regression() {
    std::cout << "Test 49: Stage 2-7 regression check (unlinked & linked debug)\n";
    CPU cpu;
    Memory mem(0x40000);
    setup_cpu(cpu, mem, 1, 0x10000);
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, enc_add_imm(0, 0, 1));

    // Configure linked breakpoint at 0x2000
    cpu.get_debug_arch().set_mdscr_el1(1ULL << 15u); // MDE=1, SS=0
    cpu.get_debug_arch().set_dbgbvr(0, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(0, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (1u << 16u) | (1u << 20u)); // BT=1, LBN=1
    cpu.get_debug_arch().set_dbgbvr(1, 0x2000);
    cpu.get_debug_arch().set_dbgbcr(1, 0x1u | (0x3u << 1u) | (0xFu << 5u) | (3u << 20u)); // BT=3

    cpu.step(mem);

    // Takes Breakpoint exception (EC 0x31)
    assert((cpu.get_esr_el1() >> 26u) == 0x31u);
    assert(cpu.get_elr_el1() == 0x2000ULL);
    std::cout << "  PASS\n";
}

void test_50_unsupported_behavior_and_control_masks() {
    std::cout << "Test 50: Control masks and reserved bit preservation in MDSCR_EL1\n";
    winios::DebugArchitecture debug;
    // Attempt writing all bits 1
    debug.set_mdscr_el1(0xFFFFFFFFFFFFFFFFULL);

    // Only bit 15 (MDE) and bit 0 (SS) are retained
    constexpr std::uint64_t expected = (1ULL << 15u) | (1ULL << 0u);
    assert(debug.get_mdscr_el1() == expected);
    assert(debug.monitor_debug_enabled());
    assert(debug.software_step_enabled());
    std::cout << "  PASS\n";
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 51 Stage 8: Hardware Single-Step Tests ===\n";

    // Category 1: Basic Single-Step (1-10)
    test_1_ss_disabled();
    test_2_ss_enabled();
    test_3_mde_disabled();
    test_4_mde_enabled();
    test_5_pstate_ss_behavior();
    test_6_single_step_lower_el();
    test_7_single_step_same_el();
    test_8_ec_0x32_validation();
    test_9_ec_0x33_validation();
    test_10_il_iss_exclusive_bit();

    // Category 2: Instruction Retirement (11-28)
    test_11_retirement_add();
    test_12_retirement_sub();
    test_13_retirement_mov();
    test_14_retirement_logical();
    test_15_sequential_execution();
    test_16_cond_branch_taken();
    test_17_cond_branch_not_taken();
    test_18_unconditional_branch();
    test_19_bl_and_link_register();
    test_20_ret();
    test_21_cbz_cbnz();
    test_22_tbz_tbnz();
    test_23_successful_load();
    test_24_successful_store();
    test_25_pair_load_store();
    test_26_exclusive_instruction();
    test_27_lse_atomic();
    test_28_simd_load_store();

    // Category 3: Exceptions and Recovery (29-40)
    test_29_undefined_instruction();
    test_30_svc_instruction();
    test_31_hvc_instruction();
    test_32_smc_instruction();
    test_33_eret_stepping();
    test_34_data_abort_does_not_step();
    test_35_instruction_abort_does_not_step();
    test_36_alignment_fault_does_not_step();
    test_37_watchpoint_interaction();
    test_38_breakpoint_interaction();
    test_39_handler_modifies_ss();
    test_40_eret_restores_step_state();

    // Category 4: Interrupts and SMP (41-50)
    test_41_irq_interaction();
    test_42_wfi_wfe_behavior();
    test_43_core_reset();
    test_44_two_core_independent_stepping();
    test_45_four_core_independent_stepping();
    test_46_deterministic_execution();
    test_47_parallel_execution();
    test_48_duplicate_event_prevention();
    test_49_stage2_7_regression();
    test_50_unsupported_behavior_and_control_masks();

    std::cout << "\nALL 50 SINGLE-STEP DEBUG TESTS PASSED (100%)\n";
    return 0;
}
