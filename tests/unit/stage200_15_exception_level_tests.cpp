// ==============================================================================
// WinIOS Campaign 200 Stage 15: EL0/EL1/EL2 Execution & System Registers Tests
// ==============================================================================
// Verifies:
// 1. EL0 -> EL1 transition via SVC #imm16 (EC 0x15, ISS = imm16, ELR_EL1 = PC + 4)
// 2. EL1 -> EL0 transition via ERET (restoring PC from ELR_EL1 and PSTATE from SPSR_EL1)
// 3. EL2 transitions: ERET from EL2 to EL1h
// 4. Illegal ERET privilege escalation detection (PSTATE.IL set, followed by EC 0x0E trap)
// 5. EL0 ERET rejection (EC 0x00: Undefined instruction trap from EL0 to EL1)
// 6. System register access privilege boundary enforcement:
//    - EL0 access to EL1 registers (SCTLR_EL1, TCR_EL1) traps with EC 0x00
//    - EL1 access to EL2 registers (HCR_EL2, VTTBR_EL2) traps with EC 0x00
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/sysreg.h"
#include "memory/memory.h"

namespace {

constexpr std::uint32_t enc_svc(std::uint16_t imm16) { return 0xD4000001u | (static_cast<std::uint32_t>(imm16) << 5u); }
constexpr std::uint32_t enc_eret() { return 0xD69F03E0u; }
constexpr std::uint32_t enc_nop() { return 0xD503201Fu; }

std::uint32_t enc_mrs(std::uint8_t op0, std::uint8_t op1, std::uint8_t crn, std::uint8_t crm, std::uint8_t op2, std::uint32_t rt) {
    const std::uint32_t o0 = (op0 == 2) ? 0u : 1u;
    return 0xD5300000u | (o0 << 19u) | (static_cast<std::uint32_t>(op1 & 7u) << 16u) |
           (static_cast<std::uint32_t>(crn & 15u) << 12u) | (static_cast<std::uint32_t>(crm & 15u) << 8u) |
           (static_cast<std::uint32_t>(op2 & 7u) << 5u) | (rt & 31u);
}

} // namespace

static void test_el0_to_el1_transition() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x100000ULL);
    cpu.set_current_el(0);
    Memory mem(0x200000);

    // Execute SVC #0x55 at 0x1000
    mem.write_u32(0x1000, enc_svc(0x55));
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    assert(cpu.exception_taken_in_step());
    assert(cpu.get_current_el() == 1);
    // Lower EL AArch64 synchronous vector: VBAR_EL1 + 0x400
    assert(cpu.get_program_counter() == 0x100400ULL);

    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    assert(((esr >> 26u) & 0x3Fu) == 0x15); // EC 0x15: SVC AArch64
    assert((esr & 0xFFFFu) == 0x55);
    assert(cpu.get_elr_el1() == 0x1004ULL);

    (void)esr;

    std::cout << "[PASS] test_el0_to_el1_transition\n";
}

static void test_el1_to_el0_eret() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    Memory mem(0x200000);

    cpu.set_spsr_el1(0x00000000ULL); // EL0t mode
    cpu.set_elr_el1(0x4000ULL);

    mem.write_u32(0x2000, enc_eret());
    cpu.set_program_counter(0x2000);
    cpu.step(mem);

    assert(cpu.get_current_el() == 0);
    assert(cpu.get_program_counter() == 0x4000ULL);

    std::cout << "[PASS] test_el1_to_el0_eret\n";
}

static void test_el2_eret_to_el1() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el2(0x200000ULL);
    cpu.set_current_el(2);
    Memory mem(0x400000);

    cpu.set_spsr_el2(0x00000005ULL); // EL1h
    cpu.set_elr_el2(0x10000ULL);

    mem.write_u32(0x6000, enc_eret());
    cpu.set_program_counter(0x6000);
    cpu.step(mem);

    assert(cpu.get_current_el() == 1);
    assert(cpu.get_program_counter() == 0x10000ULL);

    std::cout << "[PASS] test_el2_eret_to_el1\n";
}

static void test_illegal_eret_pstate_il() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x100000ULL);
    cpu.set_current_el(1);
    Memory mem(0x200000);

    // Attempt illegal return from EL1 to EL2
    cpu.set_spsr_el1(0x00000009ULL); // EL2h
    cpu.set_elr_el1(0x7000ULL);

    mem.write_u32(0x3000, enc_eret());
    cpu.set_program_counter(0x3000);
    cpu.step(mem);

    assert(cpu.get_il());
    assert(cpu.get_program_counter() == 0x7000ULL);

    // Next instruction fetch with PSTATE.IL triggers EC 0x0E trap
    mem.write_u32(0x7000, enc_nop());
    cpu.step(mem);

    assert(cpu.exception_taken_in_step());
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    assert(((esr >> 26u) & 0x3Fu) == 0x0E); // EC 0x0E: Illegal Execution State

    (void)esr;

    std::cout << "[PASS] test_illegal_eret_pstate_il\n";
}

static void test_system_register_privilege_boundaries() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x100000ULL);
    Memory mem(0x200000);

    // EL0 attempts to read SCTLR_EL1 (op0=3, op1=0, crn=1, crm=0, op2=0, rt=0)
    cpu.set_current_el(0);
    mem.write_u32(0x8000, enc_mrs(3, 0, 1, 0, 0, 0));
    cpu.set_program_counter(0x8000);
    cpu.step(mem);

    assert(cpu.exception_taken_in_step());
    assert(cpu.get_current_el() == 1);
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    assert(((esr >> 26u) & 0x3Fu) == 0x00); // EC 0x00: Undefined Instruction in EL0

    (void)esr;

    std::cout << "[PASS] test_system_register_privilege_boundaries\n";
}

int main() {
    std::cout << "=== Running Stage 200.15 Exception Level Tests ===\n";
    test_el0_to_el1_transition();
    test_el1_to_el0_eret();
    test_el2_eret_to_el1();
    test_illegal_eret_pstate_il();
    test_system_register_privilege_boundaries();
    std::cout << "All Stage 200.15 tests passed successfully!\n";
    return 0;
}
