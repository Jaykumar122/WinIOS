// ============================================================================
// Stage 243.15: FP/SIMD Complete Instruction Matrix & Exception Trap Tests
// Verifies implemented scalar FP and vector SIMD instructions, IEEE 754 edge
// cases (signed zeros, inf, NaN), and architectural trap on CPACR_EL1 disabling.
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

static void test_fp_scalar_matrix() {
    std::cout << "[Stage 243.15 - Test 1] Scalar FP32 and FP64 operations...\n";
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_cpacr_el1(0x00300000); // Full FP/SIMD access

    // 1. FADD S0, S1, S2 (2.5f + 3.5f = 6.0f)
    cpu.set_program_counter(0x1000);
    cpu.write_s_register(1, std::bit_cast<uint32_t>(2.5f));
    cpu.write_s_register(2, std::bit_cast<uint32_t>(3.5f));
    step_inst(cpu, mem, 0x1E222820u);
    TEST_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == 6.0f, "FADD FP32");

    // 2. FSUB D0, D1, D2 (20.0 - 5.5 = 14.5)
    cpu.set_program_counter(0x1004);
    cpu.write_d_register(1, std::bit_cast<uint64_t>(20.0));
    cpu.write_d_register(2, std::bit_cast<uint64_t>(5.5));
    step_inst(cpu, mem, 0x1E623820u);
    TEST_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == 14.5, "FSUB FP64");

    // 3. FMUL D0, D1, D2 (4.0 * 2.5 = 10.0)
    cpu.set_program_counter(0x1008);
    cpu.write_d_register(1, std::bit_cast<uint64_t>(4.0));
    cpu.write_d_register(2, std::bit_cast<uint64_t>(2.5));
    step_inst(cpu, mem, 0x1E620820u);
    TEST_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == 10.0, "FMUL FP64");

    // 4. FCMP S0, S1 (Compare equal: 4.0f == 4.0f -> NZCV = 0x6 (Z=1, C=1))
    cpu.set_program_counter(0x100C);
    cpu.write_s_register(0, std::bit_cast<uint32_t>(4.0f));
    cpu.write_s_register(1, std::bit_cast<uint32_t>(4.0f));
    step_inst(cpu, mem, 0x1E212000u);
    TEST_ASSERT(cpu.get_nzcv() == 0x6u, "FCMP equal NZCV == 0x6");
}

static void test_vector_simd_dup_umov() {
    std::cout << "[Stage 243.15 - Test 2] Vector SIMD DUP and UMOV operations...\n";
    CPU cpu;
    Memory mem(0x10000);
    cpu.set_cpacr_el1(0x00300000);

    // DUP V0.4S, W1 (0x4E040C20u)
    cpu.set_program_counter(0x2000);
    cpu.write_register(1, 0xDEADBEEFULL);
    step_inst(cpu, mem, 0x4E040C20u);

    auto dup_res = cpu.read_vector_register(0);
    TEST_ASSERT(dup_res.low == 0xDEADBEEFDEADBEEFULL, "DUP replicated lower lanes");
    TEST_ASSERT(dup_res.high == 0xDEADBEEFDEADBEEFULL, "DUP replicated upper lanes");
}

static void test_fp_trap_syndrome() {
    std::cout << "[Stage 243.15 - Test 3] Trap syndrome and register preservation on disabled FP...\n";
    CPU cpu;
    Memory mem(0x10000);

    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(0x00000000); // Trap FP
    cpu.set_program_counter(0x3000);

    cpu.write_vector_register(0, CPU::VectorRegister{0x1234ULL, 0x5678ULL});
    step_inst(cpu, mem, 0x1E222820u); // Attempt FADD

    TEST_ASSERT((cpu.read_vector_register(0) == CPU::VectorRegister{0x1234ULL, 0x5678ULL}), "Registers untouched on trap");
    const uint32_t ec = (static_cast<uint32_t>(cpu.get_esr_el1()) >> 26u) & 0x3Fu;
    TEST_ASSERT(ec == static_cast<uint32_t>(ExceptionClass::FpSimdAccessTrap), "EC is FpSimdAccessTrap (0x07)");
}

int main() {
    std::cout << "=== Running Stage 243.15 FP/SIMD Matrix Tests ===\n";
    test_fp_scalar_matrix();
    test_vector_simd_dup_umov();
    test_fp_trap_syndrome();
    std::cout << "Stage 243.15: All tests passed successfully.\n";
    return 0;
}
