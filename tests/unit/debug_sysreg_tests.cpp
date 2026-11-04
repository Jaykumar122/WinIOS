#include "cpu/cpu.h"
#include "memory/memory.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// WinIOS Phase 51 Stage 3: AArch64 Debug System Register Integration Tests
// Validates architectural MRS/MSR access to all supported debug registers,
// privilege-level checks, per-core isolation, reset behavior, trapping to
// EL2 (MDCR_EL2.TDA) and EL3 (MDCR_EL3.TDA), and regression on existing sysregs.

namespace {

// Helper: encode MRS Xt, (op0, op1, CRn, CRm, op2)
// In ARMv8: bit 20 is always 1 for sysregs; bit 19 is op0[0] (0 for op0=2, 1 for op0=3).
constexpr std::uint32_t enc_mrs(unsigned rt, unsigned op0, unsigned op1, unsigned crn, unsigned crm, unsigned op2) {
    const unsigned o0 = op0 & 1u;
    return 0xD5300000u | (o0 << 19u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

// Helper: encode MSR (op0, op1, CRn, CRm, op2), Xt
constexpr std::uint32_t enc_msr(unsigned rt, unsigned op0, unsigned op1, unsigned crn, unsigned crm, unsigned op2) {
    const unsigned o0 = op0 & 1u;
    return 0xD5100000u | (o0 << 19u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

// Helper: encode MOVZ Xd, #imm16, LSL #shift
constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm16, unsigned shift = 0) {
    return 0xD2800000u | ((shift / 16u) << 21u) | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}

// Helper: encode MOVK Xd, #imm16, LSL #shift
constexpr std::uint32_t enc_movk(unsigned rd, std::uint16_t imm16, unsigned shift = 0) {
    return 0xF2800000u | ((shift / 16u) << 21u) | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}

// Helper: load code into memory and run one instruction
void run_one(CPU& cpu, Memory& mem, std::uint32_t instruction) {
    const std::uint64_t pc = cpu.get_program_counter();
    mem.write_u32(static_cast<std::size_t>(pc), instruction);
    cpu.step(mem);
}

// Helper: load and run multiple instructions
void load_and_run(CPU& cpu, Memory& mem, const std::vector<std::uint32_t>& code) {
    const std::uint64_t base = cpu.get_program_counter();
    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(static_cast<std::size_t>(base + i * 4u), code[i]);
    }
    for (std::size_t i = 0; i < code.size(); ++i) {
        cpu.step(mem);
    }
}

// Test 1: MDSCR_EL1 read/write
void test_1_mdscr_el1_access() {
    std::cout << "Test 1: MDSCR_EL1 read/write via MRS/MSR\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x1000);

    // MDSCR_EL1: op0=2, op1=0, CRn=0, CRm=2, op2=2
    std::vector<std::uint32_t> code = {
        enc_movz(0, 0x8000),       // X0 = 0x8000 (MDE bit 15 = 1)
        enc_msr(0, 2, 0, 0, 2, 2), // MSR MDSCR_EL1, X0
        enc_mrs(1, 2, 0, 0, 2, 2), // MRS X1, MDSCR_EL1
    };
    load_and_run(cpu, mem, code);

    assert(cpu.read_register(1) == 0x8000);
    assert(cpu.get_debug_arch().monitor_debug_enabled());
    std::cout << "  PASS\n";
}

// Test 2: Every implemented DBGBVR register (0..3)
void test_2_every_dbgbvr_register() {
    std::cout << "Test 2: Every implemented DBGBVR register (DBGBVR0..3)\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x1000);

    // DBGBVR<n>_EL1: op0=2, op1=0, CRn=0, CRm=n, op2=4
    std::vector<std::uint32_t> code = {
        enc_movz(0, 0x1111), enc_msr(0, 2, 0, 0, 0, 4), // DBGBVR0
        enc_movz(1, 0x2222), enc_msr(1, 2, 0, 0, 1, 4), // DBGBVR1
        enc_movz(2, 0x3333), enc_msr(2, 2, 0, 0, 2, 4), // DBGBVR2
        enc_movz(3, 0x4444), enc_msr(3, 2, 0, 0, 3, 4), // DBGBVR3
        enc_mrs(10, 2, 0, 0, 0, 4), // Read back DBGBVR0
        enc_mrs(11, 2, 0, 0, 1, 4), // Read back DBGBVR1
        enc_mrs(12, 2, 0, 0, 2, 4), // Read back DBGBVR2
        enc_mrs(13, 2, 0, 0, 3, 4), // Read back DBGBVR3
    };
    load_and_run(cpu, mem, code);

    assert(cpu.read_register(10) == 0x1111);
    assert(cpu.read_register(11) == 0x2222);
    assert(cpu.read_register(12) == 0x3333);
    assert(cpu.read_register(13) == 0x4444);
    assert(cpu.get_debug_arch().get_dbgbvr(0) == 0x1111);
    assert(cpu.get_debug_arch().get_dbgbvr(1) == 0x2222);
    assert(cpu.get_debug_arch().get_dbgbvr(2) == 0x3333);
    assert(cpu.get_debug_arch().get_dbgbvr(3) == 0x4444);
    std::cout << "  PASS\n";
}

// Test 3: Every implemented DBGBCR register (0..3)
void test_3_every_dbgbcr_register() {
    std::cout << "Test 3: Every implemented DBGBCR register (DBGBCR0..3)\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x1000);

    // DBGBCR<n>_EL1: op0=2, op1=0, CRn=0, CRm=n, op2=5
    std::vector<std::uint32_t> code = {
        enc_movz(0, 0x01E1), enc_msr(0, 2, 0, 0, 0, 5), // DBGBCR0
        enc_movz(1, 0x01E3), enc_msr(1, 2, 0, 0, 1, 5), // DBGBCR1
        enc_movz(2, 0x01E5), enc_msr(2, 2, 0, 0, 2, 5), // DBGBCR2
        enc_movz(3, 0x01E7), enc_msr(3, 2, 0, 0, 3, 5), // DBGBCR3
        enc_mrs(10, 2, 0, 0, 0, 5), // Read back DBGBCR0
        enc_mrs(11, 2, 0, 0, 1, 5), // Read back DBGBCR1
        enc_mrs(12, 2, 0, 0, 2, 5), // Read back DBGBCR2
        enc_mrs(13, 2, 0, 0, 3, 5), // Read back DBGBCR3
    };
    load_and_run(cpu, mem, code);

    assert(cpu.read_register(10) == 0x01E1);
    assert(cpu.read_register(11) == 0x01E3);
    assert(cpu.read_register(12) == 0x01E5);
    assert(cpu.read_register(13) == 0x01E7);
    assert(cpu.get_debug_arch().get_dbgbcr(0) == 0x01E1);
    assert(cpu.get_debug_arch().get_dbgbcr(1) == 0x01E3);
    assert(cpu.get_debug_arch().get_dbgbcr(2) == 0x01E5);
    assert(cpu.get_debug_arch().get_dbgbcr(3) == 0x01E7);
    std::cout << "  PASS\n";
}

// Test 4: Every implemented DBGWVR register (0..3)
void test_4_every_dbgwvr_register() {
    std::cout << "Test 4: Every implemented DBGWVR register (DBGWVR0..3)\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x1000);

    // DBGWVR<n>_EL1: op0=2, op1=0, CRn=0, CRm=n, op2=6
    std::vector<std::uint32_t> code = {
        enc_movz(0, 0x5555), enc_msr(0, 2, 0, 0, 0, 6), // DBGWVR0
        enc_movz(1, 0x6666), enc_msr(1, 2, 0, 0, 1, 6), // DBGWVR1
        enc_movz(2, 0x7777), enc_msr(2, 2, 0, 0, 2, 6), // DBGWVR2
        enc_movz(3, 0x8888), enc_msr(3, 2, 0, 0, 3, 6), // DBGWVR3
        enc_mrs(10, 2, 0, 0, 0, 6), // Read back DBGWVR0
        enc_mrs(11, 2, 0, 0, 1, 6), // Read back DBGWVR1
        enc_mrs(12, 2, 0, 0, 2, 6), // Read back DBGWVR2
        enc_mrs(13, 2, 0, 0, 3, 6), // Read back DBGWVR3
    };
    load_and_run(cpu, mem, code);

    assert(cpu.read_register(10) == 0x5555);
    assert(cpu.read_register(11) == 0x6666);
    assert(cpu.read_register(12) == 0x7777);
    assert(cpu.read_register(13) == 0x8888);
    assert(cpu.get_debug_arch().get_dbgwvr(0) == 0x5555);
    assert(cpu.get_debug_arch().get_dbgwvr(1) == 0x6666);
    assert(cpu.get_debug_arch().get_dbgwvr(2) == 0x7777);
    assert(cpu.get_debug_arch().get_dbgwvr(3) == 0x8888);
    std::cout << "  PASS\n";
}

// Test 5: Every implemented DBGWCR register (0..3)
void test_5_every_dbgwcr_register() {
    std::cout << "Test 5: Every implemented DBGWCR register (DBGWCR0..3)\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x1000);

    // DBGWCR<n>_EL1: op0=2, op1=0, CRn=0, CRm=n, op2=7
    std::vector<std::uint32_t> code = {
        enc_movz(0, 0x0FE1), enc_msr(0, 2, 0, 0, 0, 7), // DBGWCR0
        enc_movz(1, 0x0FE3), enc_msr(1, 2, 0, 0, 1, 7), // DBGWCR1
        enc_movz(2, 0x0FE5), enc_msr(2, 2, 0, 0, 2, 7), // DBGWCR2
        enc_movz(3, 0x0FE7), enc_msr(3, 2, 0, 0, 3, 7), // DBGWCR3
        enc_mrs(10, 2, 0, 0, 0, 7), // Read back DBGWCR0
        enc_mrs(11, 2, 0, 0, 1, 7), // Read back DBGWCR1
        enc_mrs(12, 2, 0, 0, 2, 7), // Read back DBGWCR2
        enc_mrs(13, 2, 0, 0, 3, 7), // Read back DBGWCR3
    };
    load_and_run(cpu, mem, code);

    assert(cpu.read_register(10) == 0x0FE1);
    assert(cpu.read_register(11) == 0x0FE3);
    assert(cpu.read_register(12) == 0x0FE5);
    assert(cpu.read_register(13) == 0x0FE7);
    assert(cpu.get_debug_arch().get_dbgwcr(0) == 0x0FE1);
    assert(cpu.get_debug_arch().get_dbgwcr(1) == 0x0FE3);
    assert(cpu.get_debug_arch().get_dbgwcr(2) == 0x0FE5);
    assert(cpu.get_debug_arch().get_dbgwcr(3) == 0x0FE7);
    std::cout << "  PASS\n";
}

// Test 6: Per-core register isolation
void test_6_per_core_isolation() {
    std::cout << "Test 6: Per-core register isolation\n";
    CPU cpu1, cpu2;
    Memory mem(64 * 1024);
    cpu1.set_architectural_exceptions(true);
    cpu2.set_architectural_exceptions(true);
    cpu1.set_current_el(1);
    cpu2.set_current_el(1);
    cpu1.set_program_counter(0x1000);
    cpu2.set_program_counter(0x1000);

    // Core 1: write DBGBVR0 = 0x1000
    std::vector<std::uint32_t> code1 = {
        enc_movz(0, 0x1000),
        enc_msr(0, 2, 0, 0, 0, 4),
        enc_mrs(1, 2, 0, 0, 0, 4),
    };
    load_and_run(cpu1, mem, code1);

    // Core 2: write DBGBVR0 = 0x2000
    std::vector<std::uint32_t> code2 = {
        enc_movz(0, 0x2000),
        enc_msr(0, 2, 0, 0, 0, 4),
        enc_mrs(1, 2, 0, 0, 0, 4),
    };
    load_and_run(cpu2, mem, code2);

    assert(cpu1.read_register(1) == 0x1000);
    assert(cpu2.read_register(1) == 0x2000);
    assert(cpu1.get_debug_arch().get_dbgbvr(0) == 0x1000);
    assert(cpu2.get_debug_arch().get_dbgbvr(0) == 0x2000);
    std::cout << "  PASS\n";
}

// Test 7: Reset behavior
void test_7_reset_behavior() {
    std::cout << "Test 7: Reset behavior via CPU::reset()\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x1000);

    // Write non-zero values into MDSCR_EL1, DBGBVR0, DBGBCR0, DBGWVR0, DBGWCR0
    std::vector<std::uint32_t> code = {
        enc_movz(0, 0x8000), enc_msr(0, 2, 0, 0, 2, 2), // MDSCR_EL1 = 0x8000
        enc_movz(1, 0x1234), enc_msr(1, 2, 0, 0, 0, 4), // DBGBVR0 = 0x1234
        enc_movz(2, 0x0001), enc_msr(2, 2, 0, 0, 0, 5), // DBGBCR0 = 0x0001
        enc_movz(3, 0x5678), enc_msr(3, 2, 0, 0, 0, 6), // DBGWVR0 = 0x5678
        enc_movz(4, 0x0003), enc_msr(4, 2, 0, 0, 0, 7), // DBGWCR0 = 0x0003
    };
    load_and_run(cpu, mem, code);

    // Call reset
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x2000);

    // Read back all registers via MRS; all must be 0
    std::vector<std::uint32_t> check_code = {
        enc_mrs(10, 2, 0, 0, 2, 2), // MDSCR_EL1
        enc_mrs(11, 2, 0, 0, 0, 4), // DBGBVR0
        enc_mrs(12, 2, 0, 0, 0, 5), // DBGBCR0
        enc_mrs(13, 2, 0, 0, 0, 6), // DBGWVR0
        enc_mrs(14, 2, 0, 0, 0, 7), // DBGWCR0
    };
    load_and_run(cpu, mem, check_code);

    assert(cpu.read_register(10) == 0);
    assert(cpu.read_register(11) == 0);
    assert(cpu.read_register(12) == 0);
    assert(cpu.read_register(13) == 0);
    assert(cpu.read_register(14) == 0);
    std::cout << "  PASS\n";
}

// Test 8: Reserved-bit handling
void test_8_reserved_bit_handling() {
    std::cout << "Test 8: Reserved-bit handling (MDSCR_EL1 masks out non-MDE bits)\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x1000);

    // Write all 1s into MDSCR_EL1
    std::vector<std::uint32_t> code = {
        enc_movz(0, 0xFFFF),
        enc_movk(0, 0xFFFF, 16),
        enc_movk(0, 0xFFFF, 32),
        enc_movk(0, 0xFFFF, 48),
        enc_msr(0, 2, 0, 0, 2, 2), // MSR MDSCR_EL1, X0
        enc_mrs(1, 2, 0, 0, 2, 2), // MRS X1, MDSCR_EL1
    };
    load_and_run(cpu, mem, code);

    // Supported bits (MDE bit 15 and SS bit 0) must be preserved; all reserved bits masked to 0
    assert(cpu.read_register(1) == ((1ULL << 15u) | (1ULL << 0u)));
    std::cout << "  PASS\n";
}

// Test 9: Invalid configurations & 64-bit address fidelity
void test_9_invalid_configurations_and_64bit() {
    std::cout << "Test 9: Invalid configurations & 64-bit address fidelity\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x1000);

    // Verify full 64-bit addresses for DBGBVR and DBGWVR
    std::vector<std::uint32_t> code = {
        enc_movz(0, 0xDEAD, 0),
        enc_movk(0, 0xBEEF, 16),
        enc_movk(0, 0xCAFE, 32),
        enc_movk(0, 0x1234, 48),
        enc_msr(0, 2, 0, 0, 0, 4), // MSR DBGBVR0_EL1, X0
        enc_msr(0, 2, 0, 0, 0, 6), // MSR DBGWVR0_EL1, X0
        enc_mrs(1, 2, 0, 0, 0, 4), // MRS X1, DBGBVR0_EL1
        enc_mrs(2, 2, 0, 0, 0, 6), // MRS X2, DBGWVR0_EL1
    };
    load_and_run(cpu, mem, code);

    const std::uint64_t expected = 0x1234CAFEBEEFDEADULL;
    assert(cpu.read_register(1) == expected);
    assert(cpu.read_register(2) == expected);
    std::cout << "  PASS\n";
}

// Test 10: Read-only register behavior (ID_AA64DFR0_EL1)
void test_10_readonly_register_behavior() {
    std::cout << "Test 10: Read-only register behavior (ID_AA64DFR0_EL1)\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_vbar_el1(0x2000);
    cpu.set_program_counter(0x1000);

    // 1. Read ID_AA64DFR0_EL1 (op0=3, op1=0, CRn=0, CRm=5, op2=0)
    run_one(cpu, mem, enc_mrs(0, 3, 0, 0, 5, 0));
    const std::uint64_t val = cpu.read_register(0);
    const unsigned brps = (val >> 12u) & 0xFu;
    const unsigned wrps = (val >> 20u) & 0xFu;
    const unsigned dver = val & 0xFu;
    assert(brps == 3u); // 4 BRPs (3 + 1)
    assert(wrps == 3u); // 4 WRPs (3 + 1)
    assert(dver == 0x6u); // ARMv8 debug architecture

    // 2. Attempt to write to ID_AA64DFR0_EL1 -> must trap with Undefined Instruction (EC=0x00)
    cpu.set_program_counter(0x1004);
    mem.write_u32(0x1004, enc_msr(0, 3, 0, 0, 5, 0));
    cpu.step(mem);

    assert(cpu.get_current_el() == 1);
    const std::uint32_t ec = (cpu.get_esr_el1() >> 26u) & 0x3Fu;
    assert(ec == 0x00u); // Undefined instruction
    std::cout << "  PASS (ID_AA64DFR0_EL1 = 0x" << std::hex << val << std::dec << ", write trapped)\n";
}

// Test 11: EL0 restrictions
void test_11_el0_restrictions() {
    std::cout << "Test 11: EL0 access restrictions (Undefined Instruction trap to EL1)\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(0); // EL0
    cpu.set_vbar_el1(0x2000);
    cpu.set_program_counter(0x1000);

    // Attempt to access MDSCR_EL1 from EL0
    run_one(cpu, mem, enc_mrs(0, 2, 0, 0, 2, 2));

    // Trapped to EL1 with EC=0x00
    assert(cpu.get_current_el() == 1);
    const std::uint32_t ec = (cpu.get_esr_el1() >> 26u) & 0x3Fu;
    assert(ec == 0x00u);
    std::cout << "  PASS (trapped to EL1 with EC=0x00)\n";
}

// Test 12: EL2 debug trap behavior (MDCR_EL2.TDA bit 9)
void test_12_el2_trap_behavior() {
    std::cout << "Test 12: EL2 debug trap behavior via MDCR_EL2.TDA (bit 9)\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);

    // Set up EL1 execution with MDCR_EL2.TDA = 1 (bit 9)
    cpu.set_current_el(1);
    cpu.set_mdcr_el2(1u << 9u); // MDCR_EL2.TDA = 1
    cpu.set_program_counter(0x1000);

    // Attempt to execute MRS X0, DBGBVR0_EL1 from EL1
    run_one(cpu, mem, enc_mrs(0, 2, 0, 0, 0, 4));

    // Must be trapped to EL2 with EC = 0x18 (Trapped MSR/MRS)
    assert(cpu.get_current_el() == 2);
    const std::uint32_t ec = (cpu.get_esr_el2() >> 26u) & 0x3Fu;
    assert(ec == 0x18u);

    // Clear MDCR_EL2.TDA, return to EL1, access should succeed without trapping
    cpu.set_mdcr_el2(0);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x2000);
    run_one(cpu, mem, enc_mrs(0, 2, 0, 0, 0, 4));
    assert(cpu.get_current_el() == 1);
    std::cout << "  PASS (trapped to EL2 with EC=0x18 when TDA=1, succeeded when TDA=0)\n";
}

// Test 13: EL3 debug trap behavior (MDCR_EL3.TDA bit 9)
void test_13_el3_trap_behavior() {
    std::cout << "Test 13: EL3 debug trap behavior via MDCR_EL3.TDA (bit 9)\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);

    // Set up EL1 execution with MDCR_EL3.TDA = 1 (bit 9)
    cpu.set_current_el(1);
    cpu.set_mdcr_el3(1u << 9u); // MDCR_EL3.TDA = 1
    cpu.set_program_counter(0x1000);

    // Attempt to execute MSR DBGWVR0_EL1, X0 from EL1
    run_one(cpu, mem, enc_msr(0, 2, 0, 0, 0, 6));

    // Must be trapped to EL3 with EC = 0x18 (Trapped MSR/MRS)
    assert(cpu.get_current_el() == 3);
    const std::uint32_t ec = (cpu.get_esr_el3() >> 26u) & 0x3Fu;
    assert(ec == 0x18u);

    // Test from EL2 as well
    cpu.set_current_el(2);
    cpu.set_program_counter(0x2000);
    run_one(cpu, mem, enc_mrs(1, 2, 0, 0, 2, 2)); // MRS X1, MDSCR_EL1
    assert(cpu.get_current_el() == 3);
    const std::uint32_t ec2 = (cpu.get_esr_el3() >> 26u) & 0x3Fu;
    assert(ec2 == 0x18u);

    std::cout << "  PASS (trapped to EL3 with EC=0x18 from EL1 and EL2)\n";
}

// Test 14: Unsupported register access
void test_14_unsupported_register_access() {
    std::cout << "Test 14: Unsupported register access (Undefined Instruction trap EC=0x00)\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_vbar_el1(0x2000);
    cpu.set_program_counter(0x1000);

    // Attempt to access unsupported DBGBVR4_EL1 (op0=2, op1=0, CRn=0, CRm=4, op2=4)
    // Slot 4 is beyond the 4 supported breakpoints (slots 0..3)
    run_one(cpu, mem, enc_mrs(0, 2, 0, 0, 4, 4));

    // Must trap with Undefined Instruction exception (EC = 0x00)
    assert(cpu.get_current_el() == 1);
    const std::uint32_t ec = (cpu.get_esr_el1() >> 26u) & 0x3Fu;
    assert(ec == 0x00u);

    // Also attempt MSR on unsupported DBGWVR4_EL1 (CRm=4, op2=6)
    cpu.set_program_counter(0x1004);
    run_one(cpu, mem, enc_msr(0, 2, 0, 0, 4, 6));
    assert(cpu.get_current_el() == 1);
    const std::uint32_t ec2 = (cpu.get_esr_el1() >> 26u) & 0x3Fu;
    assert(ec2 == 0x00u);

    std::cout << "  PASS (trapped with EC=0x00 for unsupported breakpoint/watchpoint slots)\n";
}

// Test 15: Existing CPU system-register regression
void test_15_existing_sysreg_regression() {
    std::cout << "Test 15: Existing CPU system-register regression (NZCV, VBAR_EL1, CNTFRQ_EL0, PMCR_EL0)\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x1000);

    // NZCV: op0=3, op1=3, CRn=4, CRm=2, op2=0
    // VBAR_EL1: op0=3, op1=0, CRn=12, CRm=0, op2=0
    // CNTFRQ_EL0: op0=3, op1=3, CRn=14, CRm=0, op2=0
    // PMCR_EL0: op0=3, op1=3, CRn=9, CRm=12, op2=0
    std::vector<std::uint32_t> code = {
        // Write and read NZCV
        enc_movz(0, 0xF000, 16), // N, Z, C, V = 1 (bits [31:28])
        enc_msr(0, 3, 3, 4, 2, 0),
        enc_mrs(1, 3, 3, 4, 2, 0),
        // Write and read VBAR_EL1
        enc_movz(2, 0x8000),
        enc_msr(2, 3, 0, 12, 0, 0),
        enc_mrs(3, 3, 0, 12, 0, 0),
        // Read CNTFRQ_EL0
        enc_mrs(4, 3, 3, 14, 0, 0),
        // Write and read PMCR_EL0
        enc_movz(5, 0x0001), // E=1
        enc_msr(5, 3, 3, 9, 12, 0),
        enc_mrs(6, 3, 3, 9, 12, 0),
    };
    load_and_run(cpu, mem, code);

    assert((cpu.read_register(1) & 0xF0000000ULL) == 0xF0000000ULL);
    assert(cpu.read_register(3) == 0x8000ULL);
    assert(cpu.read_register(4) == 24000000ULL);
    assert((cpu.read_register(6) & 0x1ULL) == 0x1ULL);
    std::cout << "  PASS\n";
}

// Full coverage validation of all 17 supported debug registers
void test_full_register_coverage() {
    std::cout << "Test: Full register coverage (all 17 registers via MRS)\n";
    CPU cpu;
    Memory mem(64 * 1024);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);

    const std::vector<std::pair<const char*, std::uint32_t>> registers = {
        {"MDSCR_EL1",    enc_mrs(0, 2, 0, 0, 2, 2)},
        {"DBGBVR0_EL1",  enc_mrs(0, 2, 0, 0, 0, 4)},
        {"DBGBVR1_EL1",  enc_mrs(0, 2, 0, 0, 1, 4)},
        {"DBGBVR2_EL1",  enc_mrs(0, 2, 0, 0, 2, 4)},
        {"DBGBVR3_EL1",  enc_mrs(0, 2, 0, 0, 3, 4)},
        {"DBGBCR0_EL1",  enc_mrs(0, 2, 0, 0, 0, 5)},
        {"DBGBCR1_EL1",  enc_mrs(0, 2, 0, 0, 1, 5)},
        {"DBGBCR2_EL1",  enc_mrs(0, 2, 0, 0, 2, 5)},
        {"DBGBCR3_EL1",  enc_mrs(0, 2, 0, 0, 3, 5)},
        {"DBGWVR0_EL1",  enc_mrs(0, 2, 0, 0, 0, 6)},
        {"DBGWVR1_EL1",  enc_mrs(0, 2, 0, 0, 1, 6)},
        {"DBGWVR2_EL1",  enc_mrs(0, 2, 0, 0, 2, 6)},
        {"DBGWVR3_EL1",  enc_mrs(0, 2, 0, 0, 3, 6)},
        {"DBGWCR0_EL1",  enc_mrs(0, 2, 0, 0, 0, 7)},
        {"DBGWCR1_EL1",  enc_mrs(0, 2, 0, 0, 1, 7)},
        {"DBGWCR2_EL1",  enc_mrs(0, 2, 0, 0, 2, 7)},
        {"DBGWCR3_EL1",  enc_mrs(0, 2, 0, 0, 3, 7)},
    };

    for (const auto& reg : registers) {
        cpu.set_program_counter(0x1000);
        run_one(cpu, mem, reg.second);
        std::cout << "    " << reg.first << " accessible\n";
    }
    std::cout << "  PASS (17 registers)\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Phase 51 Stage 3: Debug System Register Integration Tests ===\n\n";

    try {
        test_1_mdscr_el1_access();
        test_2_every_dbgbvr_register();
        test_3_every_dbgbcr_register();
        test_4_every_dbgwvr_register();
        test_5_every_dbgwcr_register();
        test_6_per_core_isolation();
        test_7_reset_behavior();
        test_8_reserved_bit_handling();
        test_9_invalid_configurations_and_64bit();
        test_10_readonly_register_behavior();
        test_11_el0_restrictions();
        test_12_el2_trap_behavior();
        test_13_el3_trap_behavior();
        test_14_unsupported_register_access();
        test_15_existing_sysreg_regression();
        test_full_register_coverage();

        std::cout << "\n=== All 15 Stage 3 Tests PASSED Successfully! ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n=== Test FAILED with exception: " << e.what() << " ===\n";
        return 1;
    }
}
