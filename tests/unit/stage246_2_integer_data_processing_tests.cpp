// ============================================================================
// Stage 246.2: ARM64 Integer Data Processing & Conditional Select Conformance Tests
// Verifies ADC, ADCS, SBC, SBCS (arithmetic with carry), and conditional select
// instructions (CSEL, CSINC, CSINV, CSNEG) with boundary flag conditions.
// ============================================================================

#include "cpu/cpu.h"
#include "memory/memory.h"

#include <iostream>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

static void test_adc_adcs_with_carry() {
    std::cout << "[Stage 246.2 - Test 1] ADC and ADCS with carry flag variations...\n";
    CPU cpu;
    Memory mem(0x100000);
    const uint64_t pc = 0x20000;

    // ADC X0, X1, X2: 0x9A020020u
    // Case 1: Carry flag = 0 (10 + 20 + 0 = 30)
    cpu.reset();
    cpu.set_nzcv(0); // C = 0
    cpu.write_register(1, 10);
    cpu.write_register(2, 20);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x9A020020u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 30, "ADC with C=0: 10 + 20 + 0 = 30");

    // Case 2: Carry flag = 1 (10 + 20 + 1 = 31)
    cpu.reset();
    cpu.set_nzcv(0x2); // C = 1 (bit 1)
    cpu.write_register(1, 10);
    cpu.write_register(2, 20);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x9A020020u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 31, "ADC with C=1: 10 + 20 + 1 = 31");

    // ADCS X0, X1, X2: 0xBA020020u (UINT64_MAX + 0 + carry 1 -> 0, C=1, Z=1)
    cpu.reset();
    cpu.set_nzcv(0x2); // C = 1
    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFULL);
    cpu.write_register(2, 0);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0xBA020020u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0, "ADCS wrap to 0");
    TEST_ASSERT((cpu.get_nzcv() & 0x4u) != 0, "Z flag set");
    TEST_ASSERT((cpu.get_nzcv() & 0x2u) != 0, "C flag set");
}

static void test_sbc_sbcs_with_borrow() {
    std::cout << "[Stage 246.2 - Test 2] SBC and SBCS with borrow...\n";
    CPU cpu;
    Memory mem(0x100000);
    const uint64_t pc = 0x20000;

    // In ARM64: SBC is X1 - X2 - 1 + C
    // Case 1: C = 1 (no borrow): 50 - 20 - 1 + 1 = 30
    cpu.reset();
    cpu.set_nzcv(0x2); // C = 1
    cpu.write_register(1, 50);
    cpu.write_register(2, 20);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0xDA020020u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 30, "SBC with C=1: 50 - 20 = 30");

    // Case 2: C = 0 (borrow): 50 - 20 - 1 + 0 = 29
    cpu.reset();
    cpu.set_nzcv(0x0); // C = 0
    cpu.write_register(1, 50);
    cpu.write_register(2, 20);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0xDA020020u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 29, "SBC with C=0: 50 - 20 - 1 = 29");
}

static void test_conditional_select_family() {
    std::cout << "[Stage 246.2 - Test 3] CSEL, CSINC, CSINV, CSNEG...\n";
    CPU cpu;
    Memory mem(0x100000);
    const uint64_t pc = 0x20000;

    // CSEL X0, X1, X2, EQ (0x9A820020u)
    // Z=1 (EQ holds) -> returns X1
    cpu.reset();
    cpu.set_nzcv(0x4); // Z=1
    cpu.write_register(1, 100);
    cpu.write_register(2, 200);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x9A820020u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 100, "CSEL true branch returns X1");

    // Z=0 (EQ fails) -> returns X2
    cpu.reset();
    cpu.set_nzcv(0x0); // Z=0
    cpu.write_register(1, 100);
    cpu.write_register(2, 200);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x9A820020u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 200, "CSEL false branch returns X2");

    // CSINC X0, X1, X2, EQ (0x9A820420u)
    // Z=0 -> returns X2 + 1
    cpu.reset();
    cpu.set_nzcv(0x0);
    cpu.write_register(1, 100);
    cpu.write_register(2, 200);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x9A820420u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 201, "CSINC false branch returns X2 + 1");

    // CSINV X0, X1, X2, EQ (0xDA820020u)
    // Z=0 -> returns ~X2
    cpu.reset();
    cpu.set_nzcv(0x0);
    cpu.write_register(1, 100);
    cpu.write_register(2, 0);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0xDA820020u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFFULL, "CSINV false branch returns ~0");

    // CSNEG X0, X1, X2, EQ (0xDA820420u)
    // Z=0 -> returns -X2
    cpu.reset();
    cpu.set_nzcv(0x0);
    cpu.write_register(1, 100);
    cpu.write_register(2, 42);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0xDA820420u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == static_cast<uint64_t>(-42LL), "CSNEG false branch returns -42");
}

int main() {
    std::cout << "=== Running Stage 246.2 Integer Data Processing Tests ===\n";
    test_adc_adcs_with_carry();
    test_sbc_sbcs_with_borrow();
    test_conditional_select_family();
    std::cout << "Stage 246.2: All integer data processing tests passed.\n";
    return 0;
}
