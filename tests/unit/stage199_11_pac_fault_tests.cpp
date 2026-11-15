#include <cassert>
#include <iostream>

#include "cpu/cpu.h"
#include "memory/memory.h"

namespace {

constexpr std::uint32_t encode_pacia(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10000u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autia(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11000u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacda(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10800u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autda(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11800u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }

constexpr std::uint32_t encode_braa(std::uint32_t rn, std::uint32_t rm) { return 0xD71F0800u | ((rn & 0x1Fu) << 5u) | (rm & 0x1Fu); }
constexpr std::uint32_t encode_retaa() { return 0xD65F0BFFu; }

} // namespace

void test_pac_modifier_mismatch_fault() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000ULL);
    cpu.set_current_el(1);
    Memory mem(0x100000);

    const std::uint64_t raw_ptr = 0x00007FFFF0001000ULL;
    const std::uint64_t sign_mod = 0x1111222233334444ULL;
    const std::uint64_t wrong_mod = 0x9999888877776666ULL;

    // Sign with sign_mod
    cpu.write_register(0, raw_ptr);
    cpu.write_register(1, sign_mod);
    mem.write_u32(0x1000, encode_pacia(0, 1));
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    const std::uint64_t signed_ptr = cpu.read_register(0);
    assert(signed_ptr != raw_ptr);

    // Attempt authenticate with wrong_mod
    cpu.write_register(1, wrong_mod);
    mem.write_u32(0x1004, encode_autia(0, 1));
    cpu.set_program_counter(0x1004);
    cpu.step(mem);

    // Verify FPAC trap exception occurred
    assert(cpu.exception_taken_in_step());
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    assert(ec == 0x1C); // EC 0x1C: Synchronous PAC Failure Trap
    assert(cpu.get_elr_el1() == 0x1004); // Preferred return is faulting instruction
    assert(cpu.get_far_el1() == signed_ptr); // FAR records bad pointer
    (void)signed_ptr;
    (void)esr;
    (void)ec;

    std::cout << "[PASS] test_pac_modifier_mismatch_fault" << std::endl;
}

void test_pac_data_key_fault() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000ULL);
    cpu.set_current_el(1);
    Memory mem(0x100000);

    const std::uint64_t raw_ptr = 0x0000000020004000ULL;
    const std::uint64_t mod = 0x55AA55AAULL;

    cpu.write_register(0, raw_ptr);
    cpu.write_register(1, mod);
    mem.write_u32(0x1000, encode_pacda(0, 1));
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    std::uint64_t corrupted_ptr = cpu.read_register(0) ^ (1ULL << 50); // Corrupt tag bit
    cpu.write_register(0, corrupted_ptr);

    mem.write_u32(0x1004, encode_autda(0, 1));
    cpu.set_program_counter(0x1004);
    cpu.step(mem);

    assert(cpu.exception_taken_in_step());
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    assert(((esr >> 26u) & 0x3Fu) == 0x1C);
    assert((esr & 0x1FFFFFFu) == 1u); // ISS 1: Data key fault indicator
    assert(cpu.get_far_el1() == corrupted_ptr);
    (void)esr;

    std::cout << "[PASS] test_pac_data_key_fault" << std::endl;
}

void test_pac_branch_fault() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000ULL);
    cpu.set_current_el(1);
    Memory mem(0x100000);

    const std::uint64_t bad_target = 0xDEAD000000010000ULL;
    const std::uint64_t mod = 0x1234ULL;

    cpu.write_register(0, bad_target);
    cpu.write_register(1, mod);
    mem.write_u32(0x2000, encode_braa(0, 1));
    cpu.set_program_counter(0x2000);
    cpu.step(mem);

    assert(cpu.exception_taken_in_step());
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    assert(((esr >> 26u) & 0x3Fu) == 0x1C);
    assert(cpu.get_elr_el1() == 0x2000);
    // Vector address taken (EL1 sync vector: VBAR + 0x200)
    assert(cpu.get_program_counter() == 0x80200ULL);
    (void)esr;

    std::cout << "[PASS] test_pac_branch_fault" << std::endl;
}

void test_pac_retaa_fault() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000ULL);
    cpu.set_current_el(1);
    Memory mem(0x100000);

    const std::uint64_t bad_lr = 0xBADC000000004000ULL;
    const std::uint64_t sp_val = 0x9000ULL;
    cpu.set_stack_pointer(sp_val);
    cpu.write_register(30, bad_lr);

    mem.write_u32(0x3000, encode_retaa());
    cpu.set_program_counter(0x3000);
    cpu.step(mem);

    assert(cpu.exception_taken_in_step());
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    assert(((esr >> 26u) & 0x3Fu) == 0x1C);
    assert(cpu.get_elr_el1() == 0x3000);
    assert(cpu.get_far_el1() == bad_lr);
    (void)esr;

    std::cout << "[PASS] test_pac_retaa_fault" << std::endl;
}

void test_pac_corrupt_bits_mode() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.get_pac().set_fault_mode(winios::cpu::PacFaultMode::CorruptUpperBits);
    Memory mem(0x20000);

    const std::uint64_t raw_ptr = 0x0000000000040000ULL;
    const std::uint64_t sign_mod = 0x12345678ULL;
    const std::uint64_t wrong_mod = 0x87654321ULL;

    cpu.write_register(0, raw_ptr);
    cpu.write_register(1, sign_mod);
    mem.write_u32(0x1000, encode_pacia(0, 1));
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    cpu.write_register(1, wrong_mod);
    mem.write_u32(0x1004, encode_autia(0, 1));
    cpu.set_program_counter(0x1004);
    cpu.step(mem);

    // In CorruptUpperBits mode, no trap exception is thrown; instead, upper error bit is set
    assert(!cpu.exception_taken_in_step());
    const std::uint64_t result_ptr = cpu.read_register(0);
    assert((result_ptr & winios::cpu::Arm64ePointerAuth::kPacErrorBit) != 0);
    (void)result_ptr;

    std::cout << "[PASS] test_pac_corrupt_bits_mode" << std::endl;
}

int main() {
    std::cout << "--- Stage 199.11: PAC Faults & Exception Integration Tests ---" << std::endl;
    test_pac_modifier_mismatch_fault();
    test_pac_data_key_fault();
    test_pac_branch_fault();
    test_pac_retaa_fault();
    test_pac_corrupt_bits_mode();
    std::cout << "All Stage 199.11 tests passed successfully!" << std::endl;
    return 0;
}
