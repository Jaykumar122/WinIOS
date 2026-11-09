#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "cpu/cpu.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"

#define STAGE64_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

namespace {

void step_inst(CPU& cpu, Memory& memory, std::uint32_t inst) {
    memory.write_u32(cpu.get_program_counter(), inst);
    cpu.step(memory);
}

// -----------------------------------------------------------------------------
// Suite 1: Scalar Single-Precision (FP32) Arithmetic
// -----------------------------------------------------------------------------
bool test_scalar_fp32_arithmetic() {
    std::cout << "[1] test_scalar_fp32_arithmetic...\n";
    CPU cpu;
    Memory memory(0x4000);
    cpu.set_cpacr_el1(0x00300000); // Full FP/SIMD access enabled

    // FADD S0, S1, S2 (1.25f + 2.75f = 4.0f)
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(1.25f));
    cpu.write_s_register(2, std::bit_cast<std::uint32_t>(2.75f));
    step_inst(cpu, memory, 0x1E222820u);
    STAGE64_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == 4.0f, "FADD S0=4.0f");

    // FSUB S0, S1, S2 (5.5f - 2.25f = 3.25f)
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(5.5f));
    cpu.write_s_register(2, std::bit_cast<std::uint32_t>(2.25f));
    step_inst(cpu, memory, 0x1E223820u);
    STAGE64_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == 3.25f, "FSUB S0=3.25f");

    // FMUL S0, S1, S2 (3.0f * 4.5f = 13.5f)
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(3.0f));
    cpu.write_s_register(2, std::bit_cast<std::uint32_t>(4.5f));
    step_inst(cpu, memory, 0x1E220820u);
    STAGE64_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == 13.5f, "FMUL S0=13.5f");

    // FDIV S0, S1, S2 (15.0f / 3.0f = 5.0f)
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(15.0f));
    cpu.write_s_register(2, std::bit_cast<std::uint32_t>(3.0f));
    step_inst(cpu, memory, 0x1E221820u);
    STAGE64_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == 5.0f, "FDIV S0=5.0f");

    // FSQRT S0, S1 (sqrt(16.0f) = 4.0f)
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(16.0f));
    step_inst(cpu, memory, 0x1E21C020u);
    STAGE64_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == 4.0f, "FSQRT S0=4.0f");

    // FABS S0, S1 (fabs(-7.5f) = 7.5f)
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(-7.5f));
    step_inst(cpu, memory, 0x1E20C020u);
    STAGE64_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == 7.5f, "FABS S0=7.5f");

    // FNEG S0, S1 (fneg(3.14f) = -3.14f)
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(3.14f));
    step_inst(cpu, memory, 0x1E214020u);
    STAGE64_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == -3.14f, "FNEG S0=-3.14f");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Scalar Double-Precision (FP64) Arithmetic
// -----------------------------------------------------------------------------
bool test_scalar_fp64_arithmetic() {
    std::cout << "[2] test_scalar_fp64_arithmetic...\n";
    CPU cpu;
    Memory memory(0x4000);
    cpu.set_cpacr_el1(0x00300000);

    // FADD D0, D1, D2 (10.5 + 20.25 = 30.75)
    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(10.5));
    cpu.write_d_register(2, std::bit_cast<std::uint64_t>(20.25));
    step_inst(cpu, memory, 0x1E622820u);
    STAGE64_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == 30.75, "FADD D0=30.75");

    // FSUB D0, D1, D2 (50.0 - 15.5 = 34.5)
    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(50.0));
    cpu.write_d_register(2, std::bit_cast<std::uint64_t>(15.5));
    step_inst(cpu, memory, 0x1E623820u);
    STAGE64_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == 34.5, "FSUB D0=34.5");

    // FMUL D0, D1, D2 (6.0 * 7.5 = 45.0)
    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(6.0));
    cpu.write_d_register(2, std::bit_cast<std::uint64_t>(7.5));
    step_inst(cpu, memory, 0x1E620820u);
    STAGE64_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == 45.0, "FMUL D0=45.0");

    // FDIV D0, D1, D2 (100.0 / 4.0 = 25.0)
    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(100.0));
    cpu.write_d_register(2, std::bit_cast<std::uint64_t>(4.0));
    step_inst(cpu, memory, 0x1E621820u);
    STAGE64_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == 25.0, "FDIV D0=25.0");

    // FSQRT D0, D1 (sqrt(64.0) = 8.0)
    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(64.0));
    step_inst(cpu, memory, 0x1E61C020u);
    STAGE64_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == 8.0, "FSQRT D0=8.0");

    // FABS D0, D1 (fabs(-99.9) = 99.9)
    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(-99.9));
    step_inst(cpu, memory, 0x1E60C020u);
    STAGE64_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == 99.9, "FABS D0=99.9");

    // FNEG D0, D1 (fneg(42.0) = -42.0)
    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(42.0));
    step_inst(cpu, memory, 0x1E614020u);
    STAGE64_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == -42.0, "FNEG D0=-42.0");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Floating-Point Comparisons & Condition Flags (NZCV)
// -----------------------------------------------------------------------------
bool test_fp_comparisons_nzcv() {
    std::cout << "[3] test_fp_comparisons_nzcv...\n";
    CPU cpu;
    Memory memory(0x4000);
    cpu.set_cpacr_el1(0x00300000);

    // FCMP S0, S1 (Equal): 5.0f == 5.0f -> NZCV = 0x6 (Z=1, C=1)
    cpu.write_s_register(0, std::bit_cast<std::uint32_t>(5.0f));
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(5.0f));
    step_inst(cpu, memory, 0x1E212000u);
    STAGE64_ASSERT(cpu.get_nzcv() == 0x6u, "FCMP equal NZCV == 0x6 (Z=1, C=1)");

    // FCMP S0, S1 (Less): 3.0f < 5.0f -> NZCV = 0x8 (N=1)
    cpu.write_s_register(0, std::bit_cast<std::uint32_t>(3.0f));
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(5.0f));
    step_inst(cpu, memory, 0x1E212000u);
    STAGE64_ASSERT(cpu.get_nzcv() == 0x8u, "FCMP less NZCV == 0x8 (N=1)");

    // FCMP S0, S1 (Greater): 7.0f > 5.0f -> NZCV = 0x2 (C=1)
    cpu.write_s_register(0, std::bit_cast<std::uint32_t>(7.0f));
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(5.0f));
    step_inst(cpu, memory, 0x1E212000u);
    STAGE64_ASSERT(cpu.get_nzcv() == 0x2u, "FCMP greater NZCV == 0x2 (C=1)");

    // FCMP S0, S1 (Unordered / NaN): NaN vs 5.0f -> NZCV = 0x3 (C=1, V=1)
    const float nan_val = std::numeric_limits<float>::quiet_NaN();
    cpu.write_s_register(0, std::bit_cast<std::uint32_t>(nan_val));
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(5.0f));
    step_inst(cpu, memory, 0x1E212000u);
    STAGE64_ASSERT(cpu.get_nzcv() == 0x3u, "FCMP unordered NZCV == 0x3 (C=1, V=1)");

    // FCMP D0, D1 (Double precision): 100.0 == 100.0 -> NZCV = 0x6
    cpu.write_d_register(0, std::bit_cast<std::uint64_t>(100.0));
    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(100.0));
    step_inst(cpu, memory, 0x1E612000u);
    STAGE64_ASSERT(cpu.get_nzcv() == 0x6u, "FCMP double equal NZCV == 0x6");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Floating-Point Immediate Loads (FMOV S/D, #imm8)
// -----------------------------------------------------------------------------
bool test_fp_immediate_loads() {
    std::cout << "[4] test_fp_immediate_loads...\n";
    CPU cpu;
    Memory memory(0x4000);
    cpu.set_cpacr_el1(0x00300000);

    // FMOV S0, #0 -> expanded float 2.0f (0x40000000)
    // 0x1E201000u | (0 << 13) | 0
    step_inst(cpu, memory, 0x1E201000u);
    STAGE64_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == 2.0f, "FMOV S0, #0 is 2.0f");

    // FMOV D0, #0 -> expanded double 2.0 (0x4000000000000000)
    // 0x1E601000u | (0 << 13) | 0
    step_inst(cpu, memory, 0x1E601000u);
    STAGE64_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == 2.0, "FMOV D0, #0 is 2.0");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: General-Purpose <-> Floating-Point Transfers (FMOV GP)
// -----------------------------------------------------------------------------
bool test_fp_gp_register_transfers() {
    std::cout << "[5] test_fp_gp_register_transfers...\n";
    CPU cpu;
    Memory memory(0x4000);
    cpu.set_cpacr_el1(0x00300000);

    // FMOV W0, S1 (0x1E260020u)
    cpu.write_s_register(1, 0x3F800000u); // 1.0f in IEEE 754
    step_inst(cpu, memory, 0x1E260020u);
    STAGE64_ASSERT(cpu.read_register(0) == 0x3F800000u, "FMOV W0, S1 transfers 32-bit pattern");

    // FMOV S0, W1 (0x1E270020u)
    cpu.write_register(1, 0x40490FD0u); // 3.14159f in IEEE 754
    step_inst(cpu, memory, 0x1E270020u);
    STAGE64_ASSERT(cpu.read_s_register(0) == 0x40490FD0u, "FMOV S0, W1 transfers 32-bit pattern");

    // FMOV X0, D1 (0x9E660020u)
    cpu.write_d_register(1, 0x400921FB54442D18ULL); // Pi in double
    step_inst(cpu, memory, 0x9E660020u);
    STAGE64_ASSERT(cpu.read_register(0) == 0x400921FB54442D18ULL, "FMOV X0, D1 transfers 64-bit pattern");

    // FMOV D0, X1 (0x9E670020u)
    cpu.write_register(1, 0x4024000000000000ULL); // 10.0 in double
    step_inst(cpu, memory, 0x9E670020u);
    STAGE64_ASSERT(cpu.read_d_register(0) == 0x4024000000000000ULL, "FMOV D0, X1 transfers 64-bit pattern");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Floating-Point <-> Integer Conversions
// -----------------------------------------------------------------------------
bool test_fp_integer_conversions() {
    std::cout << "[6] test_fp_integer_conversions...\n";
    CPU cpu;
    Memory memory(0x4000);
    cpu.set_cpacr_el1(0x00300000);

    // FCVTZS W0, S1 (42.9f -> 42): 0x1E380020u
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(42.9f));
    step_inst(cpu, memory, 0x1E380020u);
    STAGE64_ASSERT(static_cast<std::int32_t>(cpu.read_register(0)) == 42, "FCVTZS W0=42");

    // FCVTZS X0, D1 (-1234.7 -> -1234): 0x9E780020u
    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(-1234.7));
    step_inst(cpu, memory, 0x9E780020u);
    STAGE64_ASSERT(static_cast<std::int64_t>(cpu.read_register(0)) == -1234, "FCVTZS X0=-1234");

    // FCVTZU W0, S1 (55.8f -> 55u): 0x1E390020u
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(55.8f));
    step_inst(cpu, memory, 0x1E390020u);
    STAGE64_ASSERT(cpu.read_register(0) == 55u, "FCVTZU W0=55");

    // FCVTZU X0, D1 (9999.2 -> 9999u): 0x9E790020u
    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(9999.2));
    step_inst(cpu, memory, 0x9E790020u);
    STAGE64_ASSERT(cpu.read_register(0) == 9999u, "FCVTZU X0=9999");

    // SCVTF S0, W1 (-100 -> -100.0f): 0x1E220020u
    cpu.write_register(1, static_cast<std::uint64_t>(static_cast<std::int64_t>(-100)));
    step_inst(cpu, memory, 0x1E220020u);
    STAGE64_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == -100.0f, "SCVTF S0=-100.0f");

    // SCVTF D0, X1 (-2000000 -> -2000000.0): 0x9E620020u
    cpu.write_register(1, static_cast<std::uint64_t>(static_cast<std::int64_t>(-2000000)));
    step_inst(cpu, memory, 0x9E620020u);
    STAGE64_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == -2000000.0, "SCVTF D0=-2000000.0");

    // FCVT S0, D1 (Double to Single): 0x1E624020u
    cpu.write_d_register(1, std::bit_cast<std::uint64_t>(123.5));
    step_inst(cpu, memory, 0x1E624020u);
    STAGE64_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == 123.5f, "FCVT S0, D1 is 123.5f");

    // FCVT D0, S1 (Single to Double): 0x1E22C020u
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(87.25f));
    step_inst(cpu, memory, 0x1E22C020u);
    STAGE64_ASSERT(std::bit_cast<double>(cpu.read_d_register(0)) == 87.25, "FCVT D0, S1 is 87.25");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: ASIMD Vector Arithmetic
// -----------------------------------------------------------------------------
bool test_asimd_vector_arithmetic() {
    std::cout << "[7] test_asimd_vector_arithmetic...\n";
    CPU cpu;
    Memory memory(0x4000);
    cpu.set_cpacr_el1(0x00300000);

    // ADD V0.4S, V1.4S, V2.4S (0x4EA28420u)
    cpu.write_vector_register(1, CPU::VectorRegister{0x0000000200000001ULL, 0x0000000400000003ULL});
    cpu.write_vector_register(2, CPU::VectorRegister{0x0000000600000005ULL, 0x0000000800000007ULL});
    step_inst(cpu, memory, 0x4EA28420u);

    auto res = cpu.read_vector_register(0);
    STAGE64_ASSERT(res.low == 0x0000000800000006ULL, "ADD V0.4S low lanes match");
    STAGE64_ASSERT(res.high == 0x0000000C0000000AULL, "ADD V0.4S high lanes match");

    // SUB V0.4S, V1.4S, V2.4S (0x6EA28420u)
    cpu.write_vector_register(1, CPU::VectorRegister{0x0000000900000007ULL, 0x0000000500000003ULL});
    cpu.write_vector_register(2, CPU::VectorRegister{0x0000000200000001ULL, 0x0000000200000001ULL});
    step_inst(cpu, memory, 0x6EA28420u);

    res = cpu.read_vector_register(0);
    STAGE64_ASSERT(res.low == 0x0000000700000006ULL, "SUB V0.4S low lanes match");
    STAGE64_ASSERT(res.high == 0x0000000300000002ULL, "SUB V0.4S high lanes match");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: ASIMD Vector Bitwise Logicals
// -----------------------------------------------------------------------------
bool test_asimd_vector_bitwise_logicals() {
    std::cout << "[8] test_asimd_vector_bitwise_logicals...\n";
    CPU cpu;
    Memory memory(0x4000);
    cpu.set_cpacr_el1(0x00300000);

    // NOT V0.16B, V3.16B (0x6E205860u)
    cpu.write_vector_register(3, CPU::VectorRegister{~0ULL, 0x0123456789ABCDEFULL});
    step_inst(cpu, memory, 0x6E205860u);
    STAGE64_ASSERT(cpu.read_vector_register(0).low == 0, "NOT V0.16B low inverted");
    STAGE64_ASSERT(cpu.read_vector_register(0).high == 0xFEDCBA9876543210ULL, "NOT V0.16B high inverted");

    // ORR V0.16B, V1.16B, V2.16B (0x4EA21C20u)
    cpu.write_vector_register(1, CPU::VectorRegister{0x0F0F0F0F0F0F0F0FULL, 0xAAAAAAAAAAAAAAAAULL});
    cpu.write_vector_register(2, CPU::VectorRegister{0xF0F0F0F0F0F0F0F0ULL, 0x5555555555555555ULL});
    step_inst(cpu, memory, 0x4EA21C20u);
    STAGE64_ASSERT(cpu.read_vector_register(0).low == ~0ULL, "ORR V0.16B low is all 1s");
    STAGE64_ASSERT(cpu.read_vector_register(0).high == ~0ULL, "ORR V0.16B high is all 1s");

    // AND V0.16B, V1.16B, V2.16B (0x4E221C20u)
    cpu.write_vector_register(1, CPU::VectorRegister{0xFF00FF00FF00FF00ULL, 0x00FF00FF00FF00FFULL});
    cpu.write_vector_register(2, CPU::VectorRegister{0xF0F0F0F0F0F0F0F0ULL, 0x0F0F0F0F0F0F0F0FULL});
    step_inst(cpu, memory, 0x4E221C20u);
    STAGE64_ASSERT(cpu.read_vector_register(0).low == 0xF000F000F000F000ULL, "AND V0.16B low matches");
    STAGE64_ASSERT(cpu.read_vector_register(0).high == 0x000F000F000F000FULL, "AND V0.16B high matches");

    // EOR V0.16B, V1.16B, V2.16B (0x6E221C20u)
    cpu.write_vector_register(1, CPU::VectorRegister{0x123456789ABCDEF0ULL, 0x0FEDCBA987654321ULL});
    cpu.write_vector_register(2, CPU::VectorRegister{0x123456789ABCDEF0ULL, 0x0FEDCBA987654321ULL});
    step_inst(cpu, memory, 0x6E221C20u);
    STAGE64_ASSERT(cpu.read_vector_register(0).low == 0, "EOR self is zero (low)");
    STAGE64_ASSERT(cpu.read_vector_register(0).high == 0, "EOR self is zero (high)");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: ASIMD Element Duplication & Vector Lane Extraction
// -----------------------------------------------------------------------------
bool test_asimd_duplication_and_transfer() {
    std::cout << "[9] test_asimd_duplication_and_transfer...\n";
    CPU cpu;
    Memory memory(0x4000);
    cpu.set_cpacr_el1(0x00300000);

    // DUP V0.4S, W1 (0x4E040C20u)
    cpu.write_register(1, 0xCAFEBABEULL);
    step_inst(cpu, memory, 0x4E040C20u);
    auto dup_res = cpu.read_vector_register(0);
    STAGE64_ASSERT(dup_res.low == 0xCAFEBABECAFEBABEULL, "DUP low 2 lanes replicated");
    STAGE64_ASSERT(dup_res.high == 0xCAFEBABECAFEBABEULL, "DUP high 2 lanes replicated");

    // UMOV W0, V1.S[1] (0x0E0C3C20u)
    cpu.write_vector_register(1, CPU::VectorRegister{0x1111111122222222ULL, 0x3333333344444444ULL});
    step_inst(cpu, memory, 0x0E0C3C20u);
    STAGE64_ASSERT(cpu.read_register(0) == 0x11111111u, "UMOV extracted lane 1 correctly");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: CPACR_EL1 FPEN Access Control & Exception Trapping
// -----------------------------------------------------------------------------
bool test_cpacr_el1_access_control() {
    std::cout << "[10] test_cpacr_el1_access_control...\n";
    CPU cpu;
    Memory memory(0x4000);

    // Enable architectural exceptions
    cpu.set_architectural_exceptions(true);

    // Case A: CPACR_EL1.FPEN = 0b00 (Trap all FP/SIMD accesses)
    cpu.set_cpacr_el1(0);
    cpu.write_vector_register(0, CPU::VectorRegister{0xAAAAULL, 0xBBBBULL});

    // Attempt to execute FADD S0, S1, S2
    step_inst(cpu, memory, 0x1E222820u);

    // Architectural registers must remain intact
    STAGE64_ASSERT((cpu.read_vector_register(0) == CPU::VectorRegister{0xAAAAULL, 0xBBBBULL}),
                   "Vector register preserved upon trap");

    // Trap syndrome must be FpSimdAccessTrap (EC = 0x07)
    const std::uint32_t esr = static_cast<std::uint32_t>(cpu.get_esr_el1());
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    STAGE64_ASSERT(ec == static_cast<std::uint32_t>(ExceptionClass::FpSimdAccessTrap),
                   "ESR_EL1.EC must be FpSimdAccessTrap (0x07)");

    // Case B: CPACR_EL1.FPEN = 0b11 (Permit all accesses)
    cpu.set_cpacr_el1(0x00300000);
    cpu.set_esr_el1(0);
    cpu.write_s_register(1, std::bit_cast<std::uint32_t>(10.0f));
    cpu.write_s_register(2, std::bit_cast<std::uint32_t>(20.0f));
    step_inst(cpu, memory, 0x1E222820u);

    STAGE64_ASSERT(std::bit_cast<float>(cpu.read_s_register(0)) == 30.0f,
                   "FP operation completes normally when FPEN=0b11");
    STAGE64_ASSERT(cpu.get_esr_el1() == 0, "No exception raised when enabled");

    return true;
}

} // namespace

int main() {
    std::cout << "=== Running Stage 64 Floating-Point & ASIMD Foundation Tests ===" << std::endl;

    if (!test_scalar_fp32_arithmetic()) return 1;
    if (!test_scalar_fp64_arithmetic()) return 2;
    if (!test_fp_comparisons_nzcv()) return 3;
    if (!test_fp_immediate_loads()) return 4;
    if (!test_fp_gp_register_transfers()) return 5;
    if (!test_fp_integer_conversions()) return 6;
    if (!test_asimd_vector_arithmetic()) return 7;
    if (!test_asimd_vector_bitwise_logicals()) return 8;
    if (!test_asimd_duplication_and_transfer()) return 9;
    if (!test_cpacr_el1_access_control()) return 10;

    std::cout << "stage64_fp_simd_tests: all 10 test suites passed successfully" << std::endl;
    return 0;
}
