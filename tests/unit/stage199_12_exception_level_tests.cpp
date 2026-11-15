#include <cassert>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/sysreg.h"
#include "memory/memory.h"

namespace {

constexpr std::uint32_t enc_svc(std::uint16_t imm16) { return 0xD4000001u | (static_cast<std::uint32_t>(imm16) << 5u); }
constexpr std::uint32_t enc_eret() { return 0xD69F03E0u; }
constexpr std::uint32_t enc_nop() { return 0xD503201Fu; }

} // namespace

void test_el0_to_el1_transition() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x100000ULL);
    cpu.set_current_el(0);
    Memory mem(0x200000);

    // Execute SVC #0x42 at 0x1000
    mem.write_u32(0x1000, enc_svc(0x42));
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    assert(cpu.exception_taken_in_step());
    assert(cpu.get_current_el() == 1);
    // Lower EL (AArch64) Synchronous vector is at VBAR_EL1 + 0x400
    assert(cpu.get_program_counter() == 0x100400ULL);

    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    assert(((esr >> 26u) & 0x3Fu) == 0x15); // EC 0x15: SVC AArch64
    assert((esr & 0xFFFFu) == 0x42);         // ISS matches imm16
    assert(cpu.get_elr_el1() == 0x1004ULL);  // Preferred return is PC + 4

    std::cout << "[PASS] test_el0_to_el1_transition" << std::endl;
}

void test_el1_to_el0_eret() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    Memory mem(0x200000);

    cpu.set_spsr_el1(0x00000000ULL); // Return to EL0t with interrupts unmasked
    cpu.set_elr_el1(0x5000ULL);

    mem.write_u32(0x2000, enc_eret());
    cpu.set_program_counter(0x2000);
    cpu.step(mem);

    assert(cpu.get_current_el() == 0);
    assert(cpu.get_program_counter() == 0x5000ULL);

    std::cout << "[PASS] test_el1_to_el0_eret" << std::endl;
}

void test_el2_transitions() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el2(0x200000ULL);
    cpu.set_current_el(2);
    Memory mem(0x400000);

    // ERET from EL2 to EL1h
    cpu.set_spsr_el2(0x00000005ULL); // EL1h (CurrentEL=1, SPSel=1)
    cpu.set_elr_el2(0x10000ULL);

    mem.write_u32(0x6000, enc_eret());
    cpu.set_program_counter(0x6000);
    cpu.step(mem);

    assert(cpu.get_current_el() == 1);
    assert(cpu.get_program_counter() == 0x10000ULL);

    std::cout << "[PASS] test_el2_transitions" << std::endl;
}

void test_illegal_eret_pstate_il() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x100000ULL);
    cpu.set_current_el(1);
    Memory mem(0x200000);

    // SPSR specifying return to EL2 from EL1 (illegal privilege escalation)
    cpu.set_spsr_el1(0x00000009ULL); // EL2h
    cpu.set_elr_el1(0x7000ULL);

    mem.write_u32(0x3000, enc_eret());
    cpu.set_program_counter(0x3000);
    cpu.step(mem);

    // Illegal ERET sets PSTATE.IL and updates PC to ELR
    assert(cpu.get_il());
    assert(cpu.get_program_counter() == 0x7000ULL);

    // Next instruction fetch with PSTATE.IL set triggers Illegal Execution State trap
    mem.write_u32(0x7000, enc_nop());
    cpu.step(mem);

    assert(cpu.exception_taken_in_step());
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    assert(((esr >> 26u) & 0x3Fu) == 0x0E); // EC 0x0E: Illegal Execution State

    std::cout << "[PASS] test_illegal_eret_pstate_il" << std::endl;
}

void test_el0_eret_privilege_fault() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x100000ULL);
    cpu.set_current_el(0);
    Memory mem(0x200000);

    mem.write_u32(0x1000, enc_eret());
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    assert(cpu.exception_taken_in_step());
    assert(cpu.get_current_el() == 1);
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    assert(((esr >> 26u) & 0x3Fu) == 0x00); // EC 0x00: Undefined Instruction

    std::cout << "[PASS] test_el0_eret_privilege_fault" << std::endl;
}

int main() {
    std::cout << "--- Stage 199.12: Exception-Level Integration Tests ---" << std::endl;
    test_el0_to_el1_transition();
    test_el1_to_el0_eret();
    test_el2_transitions();
    test_illegal_eret_pstate_il();
    test_el0_eret_privilege_fault();
    std::cout << "All Stage 199.12 tests passed successfully!" << std::endl;
    return 0;
}
