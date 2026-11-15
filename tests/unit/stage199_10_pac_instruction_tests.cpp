#include <cassert>
#include <iostream>
#include <string>

#include "cpu/cpu.h"
#include "memory/memory.h"

namespace {

constexpr std::uint32_t encode_pacia(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10000u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacib(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10400u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacda(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10800u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacdb(std::uint32_t rd, std::uint32_t rn) { return 0xDAC10C00u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }

constexpr std::uint32_t encode_autia(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11000u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autib(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11400u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autda(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11800u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autdb(std::uint32_t rd, std::uint32_t rn) { return 0xDAC11C00u | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu); }

constexpr std::uint32_t encode_paciza(std::uint32_t rd) { return 0xDAC123E0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacizb(std::uint32_t rd) { return 0xDAC127E0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacdza(std::uint32_t rd) { return 0xDAC12BE0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_pacdzb(std::uint32_t rd) { return 0xDAC12FE0u | (rd & 0x1Fu); }

constexpr std::uint32_t encode_autiza(std::uint32_t rd) { return 0xDAC133E0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autizb(std::uint32_t rd) { return 0xDAC137E0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autdza(std::uint32_t rd) { return 0xDAC13BE0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_autdzb(std::uint32_t rd) { return 0xDAC13FE0u | (rd & 0x1Fu); }

constexpr std::uint32_t encode_xpaci(std::uint32_t rd) { return 0xDAC143E0u | (rd & 0x1Fu); }
constexpr std::uint32_t encode_xpacd(std::uint32_t rd) { return 0xDAC147E0u | (rd & 0x1Fu); }

constexpr std::uint32_t encode_pacga(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm) {
    return 0x9AC03000u | ((rm & 0x1Fu) << 16u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t encode_braa(std::uint32_t rn, std::uint32_t rm) { return 0xD71F0800u | ((rn & 0x1Fu) << 5u) | (rm & 0x1Fu); }
constexpr std::uint32_t encode_brab(std::uint32_t rn, std::uint32_t rm) { return 0xD71F0C00u | ((rn & 0x1Fu) << 5u) | (rm & 0x1Fu); }
constexpr std::uint32_t encode_blraa(std::uint32_t rn, std::uint32_t rm) { return 0xD73F0800u | ((rn & 0x1Fu) << 5u) | (rm & 0x1Fu); }
constexpr std::uint32_t encode_blrab(std::uint32_t rn, std::uint32_t rm) { return 0xD73F0C00u | ((rn & 0x1Fu) << 5u) | (rm & 0x1Fu); }

constexpr std::uint32_t encode_retaa() { return 0xD65F0BFFu; }
constexpr std::uint32_t encode_retab() { return 0xD65F0FFFu; }

} // namespace

void test_pac_data_processing_instructions() {
    CPU cpu;
    cpu.reset();
    Memory mem(0x20000);

    const std::uint64_t raw_ptr = 0x00007FFFF0001000ULL;
    const std::uint64_t modifier = 0x123456789ABCDEF0ULL;

    // 1. PACIA and AUTIA
    cpu.write_register(0, raw_ptr);
    cpu.write_register(1, modifier);
    mem.write_u32(0x1000, encode_pacia(0, 1));
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    const std::uint64_t signed_ptr_ia = cpu.read_register(0);
    assert(signed_ptr_ia != raw_ptr);
    assert((signed_ptr_ia & 0x0000FFFFFFFFFFFFULL) == raw_ptr);
    assert((signed_ptr_ia & 0xFFFF000000000000ULL) != 0);

    // Authenticate
    mem.write_u32(0x1004, encode_autia(0, 1));
    cpu.set_program_counter(0x1004);
    cpu.step(mem);
    assert(cpu.read_register(0) == raw_ptr);

    // 2. PACIB and AUTIB
    cpu.write_register(2, raw_ptr);
    cpu.write_register(3, modifier);
    mem.write_u32(0x1008, encode_pacib(2, 3));
    cpu.set_program_counter(0x1008);
    cpu.step(mem);
    const std::uint64_t signed_ptr_ib = cpu.read_register(2);
    assert(signed_ptr_ib != raw_ptr);
    (void)signed_ptr_ib;

    mem.write_u32(0x100C, encode_autib(2, 3));
    cpu.set_program_counter(0x100C);
    cpu.step(mem);
    assert(cpu.read_register(2) == raw_ptr);

    // 3. PACDA and AUTDA
    cpu.write_register(4, raw_ptr);
    cpu.write_register(5, modifier);
    mem.write_u32(0x1010, encode_pacda(4, 5));
    cpu.set_program_counter(0x1010);
    cpu.step(mem);
    const std::uint64_t signed_ptr_da = cpu.read_register(4);
    assert(signed_ptr_da != raw_ptr);

    mem.write_u32(0x1014, encode_autda(4, 5));
    cpu.set_program_counter(0x1014);
    cpu.step(mem);
    assert(cpu.read_register(4) == raw_ptr);

    // 4. PACDB and AUTDB
    cpu.write_register(6, raw_ptr);
    cpu.write_register(7, modifier);
    mem.write_u32(0x1018, encode_pacdb(6, 7));
    cpu.set_program_counter(0x1018);
    cpu.step(mem);
    const std::uint64_t signed_ptr_db = cpu.read_register(6);
    assert(signed_ptr_db != raw_ptr);
    (void)signed_ptr_db;

    mem.write_u32(0x101C, encode_autdb(6, 7));
    cpu.set_program_counter(0x101C);
    cpu.step(mem);
    assert(cpu.read_register(6) == raw_ptr);

    // 5. Zero-modifier variants (PACIZA/AUTIZA, PACIZB/AUTIZB, PACDZA/AUTDZA, PACDZB/AUTDZB)
    cpu.write_register(8, raw_ptr);
    mem.write_u32(0x1020, encode_paciza(8));
    mem.write_u32(0x1024, encode_autiza(8));
    cpu.set_program_counter(0x1020);
    cpu.step(mem);
    assert(cpu.read_register(8) != raw_ptr);
    cpu.step(mem);
    assert(cpu.read_register(8) == raw_ptr);

    cpu.write_register(9, raw_ptr);
    mem.write_u32(0x1028, encode_pacdza(9));
    mem.write_u32(0x102C, encode_autdza(9));
    cpu.set_program_counter(0x1028);
    cpu.step(mem);
    assert(cpu.read_register(9) != raw_ptr);
    cpu.step(mem);
    assert(cpu.read_register(9) == raw_ptr);

    // 6. Stripping: XPACI and XPACD
    cpu.write_register(10, signed_ptr_ia);
    mem.write_u32(0x1030, encode_xpaci(10));
    cpu.set_program_counter(0x1030);
    cpu.step(mem);
    assert(cpu.read_register(10) == raw_ptr);

    cpu.write_register(11, signed_ptr_da);
    mem.write_u32(0x1034, encode_xpacd(11));
    cpu.set_program_counter(0x1034);
    cpu.step(mem);
    assert(cpu.read_register(11) == raw_ptr);

    std::cout << "[PASS] test_pac_data_processing_instructions" << std::endl;
}

void test_pacga_instruction() {
    CPU cpu;
    cpu.reset();
    Memory mem(0x10000);

    const std::uint64_t val1 = 0xAAAAAAAAAAAAAAAAULL;
    const std::uint64_t val2 = 0x5555555555555555ULL;

    cpu.write_register(1, val1);
    cpu.write_register(2, val2);
    mem.write_u32(0x1000, encode_pacga(0, 1, 2)); // PACGA X0, X1, X2
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    const std::uint64_t tag = cpu.read_register(0);
    assert(tag != 0);
    assert((tag & 0xFFFFFFFF00000000ULL) != 0);
    assert((tag & 0x00000000FFFFFFFFULL) == 0); // Upper 32-bits carry signature tag
    (void)tag;

    std::cout << "[PASS] test_pacga_instruction" << std::endl;
}

void test_pac_branch_instructions() {
    CPU cpu;
    cpu.reset();
    Memory mem(0x20000);

    const std::uint64_t target_a = 0x10004ULL;
    const std::uint64_t target_b = 0x10008ULL;
    const std::uint64_t mod = 0xDEADBEEFULL;

    // Sign target_a with Key A and target_b with Key B
    const std::uint64_t signed_a = cpu.get_pac().sign_pointer(target_a, mod, winios::cpu::PacKeyType::InstructionA);
    const std::uint64_t signed_b = cpu.get_pac().sign_pointer(target_b, mod, winios::cpu::PacKeyType::InstructionB);

    // 1. BRAA
    cpu.write_register(0, signed_a);
    cpu.write_register(1, mod);
    mem.write_u32(0x1000, encode_braa(0, 1)); // BRAA X0, X1
    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    assert(cpu.get_program_counter() == target_a);

    // 2. BRAB
    cpu.write_register(2, signed_b);
    cpu.write_register(3, mod);
    mem.write_u32(0x1004, encode_brab(2, 3)); // BRAB X2, X3
    cpu.set_program_counter(0x1004);
    cpu.step(mem);
    assert(cpu.get_program_counter() == target_b);

    // 3. BLRAA
    const std::uint64_t call_pc = 0x2000;
    cpu.write_register(4, signed_a);
    cpu.write_register(5, mod);
    mem.write_u32(call_pc, encode_blraa(4, 5)); // BLRAA X4, X5
    cpu.set_program_counter(call_pc);
    cpu.step(mem);
    assert(cpu.get_program_counter() == target_a);
    assert(cpu.read_register(30) == call_pc + 4);

    // 4. BLRAB
    const std::uint64_t call_pc_b = 0x2100;
    cpu.write_register(6, signed_b);
    cpu.write_register(7, mod);
    mem.write_u32(call_pc_b, encode_blrab(6, 7)); // BLRAB X6, X7
    cpu.set_program_counter(call_pc_b);
    cpu.step(mem);
    assert(cpu.get_program_counter() == target_b);
    assert(cpu.read_register(30) == call_pc_b + 4);

    // 5. RETAA (uses LR and SP as modifier)
    const std::uint64_t ret_target_a = 0x5000;
    const std::uint64_t sp_val = 0x8000;
    cpu.set_stack_pointer(sp_val);
    const std::uint64_t signed_ret_a = cpu.get_pac().sign_pointer(ret_target_a, sp_val, winios::cpu::PacKeyType::InstructionA);
    cpu.write_register(30, signed_ret_a);

    mem.write_u32(0x3000, encode_retaa()); // RETAA
    cpu.set_program_counter(0x3000);
    cpu.step(mem);
    assert(cpu.get_program_counter() == ret_target_a);

    // 6. RETAB
    const std::uint64_t ret_target_b = 0x6000;
    const std::uint64_t signed_ret_b = cpu.get_pac().sign_pointer(ret_target_b, sp_val, winios::cpu::PacKeyType::InstructionB);
    cpu.write_register(30, signed_ret_b);

    mem.write_u32(0x3004, encode_retab()); // RETAB
    cpu.set_program_counter(0x3004);
    cpu.step(mem);
    assert(cpu.get_program_counter() == ret_target_b);

    std::cout << "[PASS] test_pac_branch_instructions" << std::endl;
}

int main() {
    std::cout << "--- Stage 199.10: PAC Instruction Semantics in CPU Tests ---" << std::endl;
    test_pac_data_processing_instructions();
    test_pacga_instruction();
    test_pac_branch_instructions();
    std::cout << "All Stage 199.10 tests passed successfully!" << std::endl;
    return 0;
}
