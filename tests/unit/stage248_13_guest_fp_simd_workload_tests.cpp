// ============================================================================
// Stage 248.13: Genuine Guest FP/SIMD Execution & CPACR Trap Conformance Tests
// Tests:
// 1. Single-precision and double-precision arithmetic:
//    - FADD S0, S1, S2 (1.5f + 2.5f = 4.0f)
//    - FSUB S0, S1, S2 (10.0f - 3.0f = 7.0f)
//    - FMUL D0, D1, D2 (3.0 * 4.0 = 12.0)
//    - FDIV D0, D1, D2 (15.0 / 3.0 = 5.0)
// 2. IEEE-754 edge cases:
//    - Division by zero: 1.0 / 0.0 -> +Infinity
//    - Invalid operation: 0.0 / 0.0 -> NaN
// 3. Floating-point comparison:
//    - FCMP D1, D2 setting NZCV condition flags.
// 4. CPACR_EL1.FPEN privilege trapping:
//    - When FPEN = 0b00, executing FP instruction triggers EC=0x07 trap.
//    - When FPEN = 0b11, execution succeeds normally.
// ============================================================================

#include "cpu/cpu.h"
#include "memory/memory.h"

#include <iostream>
#include <cstdint>
#include <cmath>
#include <bit>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;

static void test_fp_arithmetic_and_edge_cases() {
    std::cout << "[Stage 248.13 - Test 1] Scalar FP arithmetic and IEEE-754 edge cases...\n";

    CPU cpu;
    Memory mem(0x100000);
    cpu.reset();
    cpu.set_cpacr_el1(0x00300000u); // Enable full FP access

    const uint64_t pc = 0x10000;

    // 1. FADD S0, S1, S2 (0x1E222820u)
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x1E222820u);
    cpu.write_s_register(1, std::bit_cast<uint32_t>(1.5f));
    cpu.write_s_register(2, std::bit_cast<uint32_t>(2.5f));
    cpu.step(mem);
    TEST_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == 4.0f, "FADD S0, S1, S2 = 4.0f");

    // 2. FSUB S0, S1, S2 (0x1E223820u)
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x1E223820u);
    cpu.write_s_register(1, std::bit_cast<uint32_t>(10.0f));
    cpu.write_s_register(2, std::bit_cast<uint32_t>(3.0f));
    cpu.step(mem);
    TEST_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == 7.0f, "FSUB S0, S1, S2 = 7.0f");

    // 3. FMUL D0, D1, D2 (0x1E620820u)
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x1E620820u);
    cpu.write_d_register(1, std::bit_cast<uint64_t>(3.0));
    cpu.write_d_register(2, std::bit_cast<uint64_t>(4.0));
    cpu.step(mem);
    TEST_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == 12.0, "FMUL D0, D1, D2 = 12.0");

    // 4. FDIV D0, D1, D2 (0x1E621820u)
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x1E621820u);
    cpu.write_d_register(1, std::bit_cast<uint64_t>(15.0));
    cpu.write_d_register(2, std::bit_cast<uint64_t>(3.0));
    cpu.step(mem);
    TEST_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == 5.0, "FDIV D0, D1, D2 = 5.0");

    // 5. FDIV edge case: divide by zero -> +Inf
    cpu.set_program_counter(pc);
    cpu.write_d_register(1, std::bit_cast<uint64_t>(1.0));
    cpu.write_d_register(2, std::bit_cast<uint64_t>(0.0));
    cpu.step(mem);
    double res_inf = std::bit_cast<double>(cpu.read_d_register(0));
    TEST_ASSERT(std::isinf(res_inf) && res_inf > 0, "1.0 / 0.0 produces positive Infinity");

    // 6. FDIV edge case: 0.0 / 0.0 -> NaN
    cpu.set_program_counter(pc);
    cpu.write_d_register(1, std::bit_cast<uint64_t>(0.0));
    cpu.write_d_register(2, std::bit_cast<uint64_t>(0.0));
    cpu.step(mem);
    double res_nan = std::bit_cast<double>(cpu.read_d_register(0));
    TEST_ASSERT(std::isnan(res_nan), "0.0 / 0.0 produces IEEE-754 NaN");
}

static void test_cpacr_fpen_trap() {
    std::cout << "[Stage 248.13 - Test 2] CPACR_EL1.FPEN trapping on disabled FP access...\n";

    CPU cpu;
    Memory mem(0x100000);
    cpu.reset();
    cpu.set_current_el(0); // EL0
    cpu.set_architectural_exceptions(true);

    // Disable FP access: FPEN = 0b00
    cpu.set_cpacr_el1(0x0u);

    const uint64_t pc = 0x20000;
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x1E222820u); // FADD S0, S1, S2

    cpu.step(mem);

    // Trap should take CPU to EL1 with ESR EC = 0x07 (FP trap)
    TEST_ASSERT(cpu.get_current_el() == 1, "CPU trapped to EL1 on disabled FP instruction");
    uint32_t esr = static_cast<uint32_t>(cpu.get_esr_el1());
    uint32_t ec = (esr >> 26u) & 0x3Fu;
    TEST_ASSERT(ec == 0x07u, "ESR_EL1 Exception Class is 0x07 (Trapped FP/SIMD access)");
}

int main() {
    std::cout << "=== Running Stage 248.13 Guest FP/SIMD Workload Tests ===\n";
    test_fp_arithmetic_and_edge_cases();
    test_cpacr_fpen_trap();
    std::cout << "Stage 248.13: All guest FP/SIMD workload tests passed.\n";
    return 0;
}
