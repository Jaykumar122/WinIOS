// ============================================================================
// Stage 246.6: ARM64 Advanced Integer Divide, Multiply & Overflow Tests
// Verifies UDIV / SDIV division-by-zero (returns 0 without trap per ARM64),
// SDIV INT64_MIN / -1 overflow wrapping, and high 64-bit multiplications
// (UMULH and SMULH) with 128-bit boundary products.
// ============================================================================

#include "cpu/cpu.h"
#include "memory/memory.h"

#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <limits>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

static void test_udiv_sdiv_zero_and_overflow() {
    std::cout << "[Stage 246.6 - Test 1] UDIV/SDIV divide-by-zero and signed overflow...\n";
    CPU cpu;
    Memory mem(0x100000);
    const uint64_t pc = 0x20000;

    // 1. UDIV X0, X1, X2 (100 / 0 = 0 per ARM64)
    cpu.reset();
    cpu.write_register(1, 100);
    cpu.write_register(2, 0);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x9AC20820u); // UDIV X0, X1, X2
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0, "UDIV divide by zero returns 0 without trap");

    // 2. SDIV X0, X1, X2 (-100 / 0 = 0 per ARM64)
    cpu.reset();
    cpu.write_register(1, static_cast<uint64_t>(-100LL));
    cpu.write_register(2, 0);
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x9AC20C20u); // SDIV X0, X1, X2
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0, "SDIV divide by zero returns 0 without trap");

    // 3. SDIV X0, X1, X2 (INT64_MIN / -1 = INT64_MIN overflow wrap)
    cpu.reset();
    cpu.write_register(1, static_cast<uint64_t>(std::numeric_limits<int64_t>::min()));
    cpu.write_register(2, static_cast<uint64_t>(-1LL));
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x9AC20C20u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == static_cast<uint64_t>(std::numeric_limits<int64_t>::min()),
                "SDIV INT64_MIN / -1 wraps to INT64_MIN per ARM64");
}

static void test_umulh_smulh_high_products() {
    std::cout << "[Stage 246.6 - Test 2] UMULH and SMULH high 64-bit products...\n";
    CPU cpu;
    Memory mem(0x100000);
    const uint64_t pc = 0x20000;

    // 1. UMULH X0, X1, X2: 0x9BC27C20u
    // Multiply 2^32 * 2^32 = 2^64 -> high 64 bits = 1
    cpu.reset();
    cpu.write_register(1, 0x100000000ULL); // 2^32
    cpu.write_register(2, 0x100000000ULL); // 2^32
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x9BC27C20u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 1, "UMULH 2^32 * 2^32 high part is 1");

    // 2. SMULH X0, X1, X2: 0x9B427C20u
    // (-1) * (-1) = +1 in 128-bit signed -> high 64 bits = 0
    cpu.reset();
    cpu.write_register(1, static_cast<uint64_t>(-1LL));
    cpu.write_register(2, static_cast<uint64_t>(-1LL));
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x9B427C20u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0, "SMULH (-1) * (-1) high part is 0");
}

int main() {
    std::cout << "=== Running Stage 246.6 Advanced Integer Divide & Multiply Tests ===\n";
    test_udiv_sdiv_zero_and_overflow();
    test_umulh_smulh_high_products();
    std::cout << "Stage 246.6: All advanced integer tests passed.\n";
    return 0;
}
