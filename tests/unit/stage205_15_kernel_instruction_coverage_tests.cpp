#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"

using namespace winios;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_alu_operations() {
    std::cout << "[Test 1] ALU Instruction Coverage (ADD, SUB, AND, ORR, EOR, BIC)..." << std::endl;

    CPU cpu;
    Memory mem(0x10000);

    cpu.write_register(1, 100);
    cpu.write_register(2, 50);

    const std::vector<std::uint32_t> code = {
        0x8B020023U, // ADD X3, X1, X2 (150)
        0xCB020024U, // SUB X4, X1, X2 (50)
        0x8A020025U, // AND X5, X1, X2
        0xAA020026U, // ORR X6, X1, X2
        0xCA020027U, // EOR X7, X1, X2
        0x8A220028U  // BIC X8, X1, X2
    };

    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(0x1000 + (i * 4), code[i]);
    }

    cpu.set_program_counter(0x1000);
    for (std::size_t i = 0; i < code.size(); ++i) {
        cpu.step(mem);
    }

    TEST_CHECK(cpu.read_register(3) == 150, "ADD X3, X1, X2 == 150");
    TEST_CHECK(cpu.read_register(4) == 50,  "SUB X4, X1, X2 == 50");
    TEST_CHECK(cpu.read_register(5) == (100 & 50), "AND X5, X1, X2");
    TEST_CHECK(cpu.read_register(6) == (100 | 50), "ORR X6, X1, X2");
    TEST_CHECK(cpu.read_register(7) == (100 ^ 50), "EOR X7, X1, X2");
    TEST_CHECK(cpu.read_register(8) == (100 & ~50), "BIC X8, X1, X2");

    std::cout << "  -> ALU operations verified." << std::endl;
}

void test_shift_operations() {
    std::cout << "[Test 2] Shifts & Bitfield (LSL, LSR)..." << std::endl;

    CPU cpu;
    Memory mem(0x10000);

    cpu.write_register(1, 0x12345678ULL);

    // Code at 0x1000:
    // 1. LSL X2, X1, #4 (UBFM X2, X1, #60, #59) -> 0xD37CEC22
    // 2. LSR X3, X1, #4 (UBFM X3, X1, #4, #63)  -> 0xD344FC23
    const std::vector<std::uint32_t> code = {
        0xD37CEC22U,
        0xD344FC23U
    };

    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(0x1000 + (i * 4), code[i]);
    }

    cpu.set_program_counter(0x1000);
    cpu.step(mem); // Step 1: LSL
    cpu.step(mem); // Step 2: LSR

    TEST_CHECK(cpu.read_register(2) == (0x12345678ULL << 4), "LSL X2, X1, #4");
    TEST_CHECK(cpu.read_register(3) == (0x12345678ULL >> 4), "LSR X3, X1, #4");

    std::cout << "  -> Shift operations verified." << std::endl;
}

void test_pc_relative_addressing() {
    std::cout << "[Test 3] PC-Relative Addressing (ADR, ADRP)..." << std::endl;

    CPU cpu;
    Memory mem(0x10000);

    // Code at 0x2000:
    // 1. ADR X1, #16  (offset +16 from 0x2000 -> 0x2010) -> 0x10000081
    // 2. ADRP X2, #0  (page of 0x2004 -> 0x2000)        -> 0x90000002
    const std::vector<std::uint32_t> code = {
        0x10000081U,
        0x90000002U
    };

    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(0x2000 + (i * 4), code[i]);
    }

    cpu.set_program_counter(0x2000);
    cpu.step(mem);
    cpu.step(mem);

    TEST_CHECK(cpu.read_register(1) == 0x2010ULL, "ADR X1, #16 == 0x2010");
    TEST_CHECK(cpu.read_register(2) == 0x2000ULL, "ADRP X2, #0 == 0x2000");

    std::cout << "  -> PC-relative addressing verified." << std::endl;
}

void test_load_store_and_pair() {
    std::cout << "[Test 4] Load/Store and Load/Store Pair (STP, LDP, STR, LDR)..." << std::endl;

    CPU cpu;
    Memory mem(0x10000);

    cpu.write_register(29, 0xAAAA'BBBB'CCCC'DDDDULL);
    cpu.write_register(30, 0x1111'2222'3333'4444ULL);
    cpu.set_stack_pointer(0x8000);

    const std::vector<std::uint32_t> code = {
        0xA9BF7BFDU, // STP X29, X30, [SP, #-16]!
        0xA8C157F4U, // LDP X20, X21, [SP], #16
        0xF90013F4U, // STR X20, [SP, #32]
        0xF94013F6U  // LDR X22, [SP, #32]
    };

    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(0x3000 + (i * 4), code[i]);
    }

    cpu.set_program_counter(0x3000);
    for (std::size_t i = 0; i < code.size(); ++i) {
        cpu.step(mem);
    }

    TEST_CHECK(cpu.get_stack_pointer() == 0x8000, "Stack pointer restored after STP/LDP");
    TEST_CHECK(cpu.read_register(20) == 0xAAAA'BBBB'CCCC'DDDDULL, "LDP X20 popped X29");
    TEST_CHECK(cpu.read_register(21) == 0x1111'2222'3333'4444ULL, "LDP X21 popped X30");
    TEST_CHECK(cpu.read_register(22) == 0xAAAA'BBBB'CCCC'DDDDULL, "LDR X22 read stored value");

    std::cout << "  -> Load/Store and Pair operations verified." << std::endl;
}

void test_barriers_and_hints() {
    std::cout << "[Test 5] Barriers and Hints (ISB, DSB, DMB, NOP)..." << std::endl;

    CPU cpu;
    Memory mem(0x10000);

    const std::vector<std::pair<std::string, std::uint32_t>> insns = {
        {"NOP",     0xD503201FU},
        {"ISB",     0xD5033FDFU},
        {"DSB ISH", 0xD5033B9FU},
        {"DMB ISH", 0xD5033BBFU}
    };

    for (std::size_t i = 0; i < insns.size(); ++i) {
        mem.write_u32(0x4000 + (i * 4), insns[i].second);
    }

    cpu.set_program_counter(0x4000);
    for (std::size_t i = 0; i < insns.size(); ++i) {
        cpu.step(mem);
    }

    TEST_CHECK(cpu.get_program_counter() == 0x4010, "PC advanced past barriers");
    std::cout << "  -> Barriers and hints verified." << std::endl;
}

void test_system_registers_and_eret() {
    std::cout << "[Test 6] System Registers (MSR/MRS) and ERET..." << std::endl;

    CPU cpu;
    Memory mem(0x10000);

    // Start in EL1
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);

    cpu.write_register(1, 0x1234'5678'9ABCULL); // value for TPIDR_EL1
    cpu.write_register(4, 0x6000);              // ELR_EL1 target (PC on ERET)
    cpu.write_register(5, 0x0);                 // SPSR_EL1 (EL0t)

    // Destination at 0x6000: NOP
    mem.write_u32(0x6000, 0xD503201FU);

    const std::vector<std::pair<std::string, std::uint32_t>> code = {
        {"MSR TPIDR_EL1, X1", 0xD518D081U},
        {"MRS X2, TPIDR_EL1", 0xD538D082U},
        {"MSR ELR_EL1, X4",   0xD5184024U},
        {"MSR SPSR_EL1, X5",  0xD5184005U},
        {"ERET",              0xD69F03E0U}
    };

    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(0x5000 + (i * 4), code[i].second);
    }

    cpu.set_program_counter(0x5000);
    for (std::size_t i = 0; i < code.size(); ++i) {
        cpu.step(mem);
    }

    TEST_CHECK(cpu.read_register(2) == 0x1234'5678'9ABCULL, "MRS read back TPIDR_EL1");
    TEST_CHECK(cpu.get_current_el() == 0, "ERET dropped to EL0");
    TEST_CHECK(cpu.get_program_counter() == 0x6000, "ERET jumped to ELR_EL1 address");

    std::cout << "  -> System registers and ERET verified." << std::endl;
}

} // namespace

int main() {
    std::cout << "=== Stage 205.15 Kernel Instruction Coverage Audit Tests ===" << std::endl;

    test_alu_operations();
    test_shift_operations();
    test_pc_relative_addressing();
    test_load_store_and_pair();
    test_barriers_and_hints();
    test_system_registers_and_eret();

    if (g_failures > 0) {
        std::cerr << "FAILED: " << g_failures << " tests failed.\n";
        return 1;
    }

    std::cout << "All Stage 205.15 tests PASSED." << std::endl;
    return 0;
}
