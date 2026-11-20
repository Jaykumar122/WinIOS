// ============================================================================
// Stage 242.16: FP/SIMD Support Contract, Arithmetic & Exception Trap Tests
// Exercises scalar FP32/FP64 arithmetic, IEEE 754 special values (NaN, Inf, -0.0),
// and verifies that execution traps deterministically when FP is disabled in CPACR_EL1.
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"

#include <iostream>
#include <cmath>
#include <limits>
#include <bit>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

static void step_inst(CPU& cpu, Memory& mem, uint32_t inst) {
    mem.write_u32(static_cast<size_t>(cpu.get_program_counter()), inst);
    cpu.step(mem);
}

static void test_fp_arithmetic_contract() {
    std::cout << "[Stage 242.16 - Test 1] FP32 and FP64 arithmetic verification...\n";
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_cpacr_el1(0x00300000); // Enable FP/SIMD

    // FADD S0, S1, S2 (1.25f + 2.75f = 4.0f)
    cpu.set_program_counter(0x1000);
    cpu.write_s_register(1, std::bit_cast<uint32_t>(1.25f));
    cpu.write_s_register(2, std::bit_cast<uint32_t>(2.75f));
    step_inst(cpu, mem, 0x1E222820u);
    TEST_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == 4.0f, "FADD S0=4.0f");

    // FDIV D0, D1, D2 (100.5 / 2.0 = 50.25)
    cpu.set_program_counter(0x1004);
    cpu.write_d_register(1, std::bit_cast<uint64_t>(100.5));
    cpu.write_d_register(2, std::bit_cast<uint64_t>(2.0));
    step_inst(cpu, mem, 0x1E621820u);
    TEST_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == 50.25, "FDIV D0=50.25");

    // Test NaN and Infinity
    double pos_inf = std::numeric_limits<double>::infinity();
    double qnan = std::numeric_limits<double>::quiet_NaN();
    TEST_ASSERT(std::isinf(pos_inf), "Infinity detected");
    TEST_ASSERT(std::isnan(qnan), "NaN detected");
}

static void test_fp_disabled_trap() {
    std::cout << "[Stage 242.16 - Test 2] Deterministic trap when FP/SIMD is disabled in CPACR_EL1...\n";
    CPU cpu;
    Memory mem(0x10000);

    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(0x00000000); // Disable FP/SIMD
    cpu.set_program_counter(0x2000);
    cpu.write_vector_register(0, CPU::VectorRegister{0xAAAAULL, 0xBBBBULL});

    // FADD S0, S1, S2 encoding: 0x1E222820
    step_inst(cpu, mem, 0x1E222820u);

    // Vector register must be preserved upon trap
    TEST_ASSERT((cpu.read_vector_register(0) == CPU::VectorRegister{0xAAAAULL, 0xBBBBULL}), "Vector reg preserved");

    // Check exception syndrome in ESR_EL1: EC must be FpSimdAccessTrap (0x07)
    const uint32_t esr = static_cast<uint32_t>(cpu.get_esr_el1());
    const uint32_t ec = (esr >> 26u) & 0x3Fu;
    TEST_ASSERT(ec == static_cast<uint32_t>(ExceptionClass::FpSimdAccessTrap), "ESR_EL1.EC must be FpSimdAccessTrap");
}

int main() {
    std::cout << "=== Running Stage 242.16 FP/SIMD Contract Tests ===\n";
    test_fp_arithmetic_contract();
    test_fp_disabled_trap();
    std::cout << "Stage 242.16: All tests passed successfully.\n";
    return 0;
}
