#include <cstdint>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>

#include "cpu/cpu.h"
#include "memory/memory.h"

using namespace winios;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAILED] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_failed_tests++; \
        } \
    } while (0)

// Helper to execute a sequence of instructions on CPU
static void run_instructions(CPU& cpu, Memory& mem, const std::vector<std::uint32_t>& code, std::uint64_t start_pc = 0x1000) {
    cpu.set_program_counter(start_pc);
    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(static_cast<std::size_t>(start_pc + i * 4), code[i]);
    }
    for (std::size_t i = 0; i < code.size(); ++i) {
        cpu.step(mem);
    }
}

// -----------------------------------------------------------------------------
// 1. Integer Arithmetic & Flags (ADD, ADDS, SUB, SUBS)
// -----------------------------------------------------------------------------
static void test_integer_arithmetic_and_flags() {
    std::cout << "[RUN] test_integer_arithmetic_and_flags\n";
    Memory mem(0x20000);
    CPU cpu;

    // ADDS X0, X1, X2 (0xAB020020) with positive inputs
    cpu.write_register(1, 0x10);
    cpu.write_register(2, 0x20);
    run_instructions(cpu, mem, { 0xAB020020u }); // adds x0, x1, x2
    TEST_CHECK(cpu.read_register(0) == 0x30, "adds x0 = 0x30");
    TEST_CHECK(cpu.get_nzcv() == 0, "NZCV is 0 for positive non-overflow result");

    // ADDS X0, X1, X2 resulting in zero (Z flag)
    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFULL);
    cpu.write_register(2, 1);
    run_instructions(cpu, mem, { 0xAB020020u }); // adds x0, x1, x2
    TEST_CHECK(cpu.read_register(0) == 0, "adds x0 = 0");
    // Z flag is bit 30 (NZCV bit 2), C flag is bit 29 (NZCV bit 1)
    // In WinIOS CPU: N=bit3, Z=bit2, C=bit1, V=bit0
    TEST_CHECK((cpu.get_nzcv() & 0x4u) != 0, "Z flag set on zero sum");
    TEST_CHECK((cpu.get_nzcv() & 0x2u) != 0, "C flag set on unsigned overflow");

    // SUBS X0, X1, X2 (0xEB020020) resulting in negative (N flag)
    cpu.write_register(1, 10);
    cpu.write_register(2, 20);
    run_instructions(cpu, mem, { 0xEB020020u }); // subs x0, x1, x2
    TEST_CHECK(cpu.read_register(0) == static_cast<std::uint64_t>(-10), "subs negative result");
    TEST_CHECK((cpu.get_nzcv() & 0x8u) != 0, "N flag set on negative result");

    // Signed overflow test (V flag): 0x7FFFFFFFFFFFFFFF + 1
    cpu.write_register(1, 0x7FFFFFFFFFFFFFFFULL);
    cpu.write_register(2, 1);
    run_instructions(cpu, mem, { 0xAB020020u }); // adds x0, x1, x2
    TEST_CHECK((cpu.get_nzcv() & 0x1u) != 0, "V flag set on signed overflow");
}

// -----------------------------------------------------------------------------
// 2. Bitwise & Logical Operations (AND, ANDS, ORR, EOR, BIC)
// -----------------------------------------------------------------------------
static void test_bitwise_logical_operations() {
    std::cout << "[RUN] test_bitwise_logical_operations\n";
    Memory mem(0x20000);
    CPU cpu;

    cpu.write_register(1, 0x0F0F0F0F0F0F0F0FULL);
    cpu.write_register(2, 0x5555555555555555ULL);

    // AND X0, X1, X2 (0x8A020020) -> 0x0505...
    run_instructions(cpu, mem, { 0x8A020020u });
    TEST_CHECK(cpu.read_register(0) == 0x0505050505050505ULL, "and x0");

    // ORR X0, X1, X2 (0xAA020020) -> 0x5F5F...
    run_instructions(cpu, mem, { 0xAA020020u });
    TEST_CHECK(cpu.read_register(0) == 0x5F5F5F5F5F5F5F5FULL, "orr x0");

    // EOR X0, X1, X2 (0xCA020020) -> 0x5A5A...
    run_instructions(cpu, mem, { 0xCA020020u });
    TEST_CHECK(cpu.read_register(0) == 0x5A5A5A5A5A5A5A5AULL, "eor x0");

    // BIC X0, X1, X2 (0x8A220020) -> X1 & ~X2 = 0x0A0A...
    run_instructions(cpu, mem, { 0x8A220020u });
    TEST_CHECK(cpu.read_register(0) == 0x0A0A0A0A0A0A0A0AULL, "bic x0");

    // ANDS X0, X1, X2 setting Z flag
    cpu.write_register(1, 0xF0);
    cpu.write_register(2, 0x0F);
    run_instructions(cpu, mem, { 0xEA020020u }); // ands x0, x1, x2
    TEST_CHECK(cpu.read_register(0) == 0, "ands zero result");
    TEST_CHECK((cpu.get_nzcv() & 0x4u) != 0, "ands Z flag set");
}

// -----------------------------------------------------------------------------
// 3. Bitfield Operations (UBFM, SBFM, RBIT, REV, CLZ)
// -----------------------------------------------------------------------------
static void test_bitfield_and_reverse_operations() {
    std::cout << "[RUN] test_bitfield_and_reverse_operations\n";
    Memory mem(0x20000);
    CPU cpu;

    // LSR X0, X1, #4 using UBFM (0xD344FC20)
    cpu.write_register(1, 0x1234567890ABCDEFULL);
    run_instructions(cpu, mem, { 0xD344FC20u }); // ubfm x0, x1, #4, #63
    TEST_CHECK(cpu.read_register(0) == (0x1234567890ABCDEFULL >> 4), "ubfm lsr #4");

    // SBFM / SXTB: Sign extend byte 0x80 (-128) to 64-bit
    cpu.write_register(1, 0x80);
    run_instructions(cpu, mem, { 0x93401C20u }); // sbfm x0, x1, #0, #7
    TEST_CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFF80ULL, "sbfm sxtb");

    // CLZ X0, X1: Count leading zeros
    cpu.write_register(1, 0x0000000100000000ULL);
    run_instructions(cpu, mem, { 0xDAC01020u }); // clz x0, x1
    TEST_CHECK(cpu.read_register(0) == 31, "clz x0 == 31");

    // RBIT X0, X1: Reverse bits of 0x8000000000000000 -> 1
    cpu.write_register(1, 0x8000000000000000ULL);
    run_instructions(cpu, mem, { 0xDAC00020u }); // rbit x0, x1
    TEST_CHECK(cpu.read_register(0) == 1, "rbit x0 == 1");

    // REV X0, X1: Reverse byte order
    cpu.write_register(1, 0x0102030405060708ULL);
    run_instructions(cpu, mem, { 0xDAC00C20u }); // rev x0, x1
    TEST_CHECK(cpu.read_register(0) == 0x0807060504030201ULL, "rev x0");
}

// -----------------------------------------------------------------------------
// 4. Multiply & Divide Operations (MADD, MSUB, UDIV, SDIV)
// -----------------------------------------------------------------------------
static void test_multiply_and_divide() {
    std::cout << "[RUN] test_multiply_and_divide\n";
    Memory mem(0x20000);
    CPU cpu;

    // MADD X0, X1, X2, X3 (0x9B020C20): X0 = X3 + (X1 * X2)
    cpu.write_register(1, 7);
    cpu.write_register(2, 6);
    cpu.write_register(3, 10);
    run_instructions(cpu, mem, { 0x9B020C20u }); // madd x0, x1, x2, x3
    TEST_CHECK(cpu.read_register(0) == 52, "madd x0 == 52");

    // MSUB X0, X1, X2, X3 (0x9B028C20): X0 = X3 - (X1 * X2)
    cpu.write_register(3, 100);
    run_instructions(cpu, mem, { 0x9B028C20u }); // msub x0, x1, x2, x3
    TEST_CHECK(cpu.read_register(0) == 58, "msub x0 == 58");

    // UDIV X0, X1, X2 (0x9AC20820): 100 / 7 = 14
    cpu.write_register(1, 100);
    cpu.write_register(2, 7);
    run_instructions(cpu, mem, { 0x9AC20820u }); // udiv x0, x1, x2
    TEST_CHECK(cpu.read_register(0) == 14, "udiv x0 == 14");

    // SDIV X0, X1, X2 (0x9AC20C20): -100 / 7 = -14
    cpu.write_register(1, static_cast<std::uint64_t>(-100));
    cpu.write_register(2, 7);
    run_instructions(cpu, mem, { 0x9AC20C20u }); // sdiv x0, x1, x2
    TEST_CHECK(static_cast<std::int64_t>(cpu.read_register(0)) == -14, "sdiv x0 == -14");

    // Divide by zero boundary case: ARM64 specification mandates return 0
    cpu.write_register(1, 42);
    cpu.write_register(2, 0);
    run_instructions(cpu, mem, { 0x9AC20820u }); // udiv x0, x1, x2
    TEST_CHECK(cpu.read_register(0) == 0, "udiv by zero returns 0 per ARM64 spec");
}

// -----------------------------------------------------------------------------
// 5. Conditional Select Operations (CSEL, CSINC, CSINV, CSNEG)
// -----------------------------------------------------------------------------
static void test_conditional_select_operations() {
    std::cout << "[RUN] test_conditional_select_operations\n";
    Memory mem(0x20000);
    CPU cpu;

    cpu.write_register(1, 100);
    cpu.write_register(2, 200);

    // CSEL X0, X1, X2, EQ (0x9A820020) with Z=1 (condition true)
    cpu.set_nzcv(0x4u); // Z=1
    run_instructions(cpu, mem, { 0x9A820020u }); // csel x0, x1, x2, eq
    TEST_CHECK(cpu.read_register(0) == 100, "csel true selects x1");

    // CSEL X0, X1, X2, EQ with Z=0 (condition false)
    cpu.set_nzcv(0x0u); // Z=0
    run_instructions(cpu, mem, { 0x9A820020u }); // csel x0, x1, x2, eq
    TEST_CHECK(cpu.read_register(0) == 200, "csel false selects x2");

    // CSINC X0, X1, X2, EQ (0x9A820420): true -> X1, false -> X2 + 1
    cpu.set_nzcv(0x0u); // false
    run_instructions(cpu, mem, { 0x9A820420u }); // csinc x0, x1, x2, eq
    TEST_CHECK(cpu.read_register(0) == 201, "csinc false selects x2 + 1");

    // CSINV X0, X1, X2, EQ (0xDA820020): false -> ~X2
    cpu.write_register(2, 0);
    cpu.set_nzcv(0x0u);
    run_instructions(cpu, mem, { 0xDA820020u }); // csinv x0, x1, x2, eq
    TEST_CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFFULL, "csinv false selects ~x2");

    // CSNEG X0, X1, X2, EQ (0xDA820420): false -> -X2
    cpu.write_register(2, 50);
    cpu.set_nzcv(0x0u);
    run_instructions(cpu, mem, { 0xDA820420u }); // csneg x0, x1, x2, eq
    TEST_CHECK(static_cast<std::int64_t>(cpu.read_register(0)) == -50, "csneg false selects -x2");
}

// -----------------------------------------------------------------------------
// 6. Branching & Control Flow (B, BL, RET, CBZ, CBNZ, TBZ, TBNZ)
// -----------------------------------------------------------------------------
static void test_branching_and_control_flow() {
    std::cout << "[RUN] test_branching_and_control_flow\n";
    Memory mem(0x20000);
    CPU cpu;

    // BL #8 (0x94000002) at 0x1000 -> branches to 0x1008, sets LR (X30) to 0x1004
    cpu.set_program_counter(0x1000);
    mem.write_u32(0x1000, 0x94000002u); // bl +8
    cpu.step(mem);
    TEST_CHECK(cpu.get_program_counter() == 0x1008, "bl branches to target");
    TEST_CHECK(cpu.read_register(30) == 0x1004, "bl saves return address in lr (x30)");

    // RET (0xD65F03C0) returns to LR (0x1004)
    mem.write_u32(0x1008, 0xD65F03C0u); // ret
    cpu.step(mem);
    TEST_CHECK(cpu.get_program_counter() == 0x1004, "ret restores pc from lr");

    // CBZ X0, +8 (0xB4000040): taken when X0 == 0
    cpu.set_program_counter(0x2000);
    cpu.write_register(0, 0);
    mem.write_u32(0x2000, 0xB4000040u); // cbz x0, +8
    cpu.step(mem);
    TEST_CHECK(cpu.get_program_counter() == 0x2008, "cbz taken when zero");

    // CBNZ X0, +8 (0xB5000040): not taken when X0 == 0
    cpu.set_program_counter(0x2000);
    mem.write_u32(0x2000, 0xB5000040u); // cbnz x0, +8
    cpu.step(mem);
    TEST_CHECK(cpu.get_program_counter() == 0x2004, "cbnz not taken when zero");

    // TBZ X0, #3, +8 (0x36180040): test bit 3 is zero -> taken
    cpu.set_program_counter(0x3000);
    cpu.write_register(0, 0b00010000); // bit 3 is 0, bit 4 is 1
    mem.write_u32(0x3000, 0x36180040u); // tbz x0, #3, +8
    cpu.step(mem);
    TEST_CHECK(cpu.get_program_counter() == 0x3008, "tbz taken when bit is zero");
}

// -----------------------------------------------------------------------------
// 7. Load / Store Memory Behavior (LDR, STR, LDP, STP, LDRB, STRB)
// -----------------------------------------------------------------------------
static void test_load_store_memory_behavior() {
    std::cout << "[RUN] test_load_store_memory_behavior\n";
    Memory mem(0x20000);
    CPU cpu;

    // STR X0, [X1, #16] (0xF9000820)
    cpu.write_register(0, 0x1122334455667788ULL);
    cpu.write_register(1, 0x4000);
    run_instructions(cpu, mem, { 0xF9000820u });
    TEST_CHECK(mem.read_u64(0x4010) == 0x1122334455667788ULL, "str x0 to [x1+16]");

    // LDR X2, [X1, #16] (0xF9400822)
    run_instructions(cpu, mem, { 0xF9400822u });
    TEST_CHECK(cpu.read_register(2) == 0x1122334455667788ULL, "ldr x2 from [x1+16]");

    // STP X0, X2, [X1, #32] (0xA9010820)
    run_instructions(cpu, mem, { 0xA9010820u }); // stp x0, x2, [x1, #16]
    TEST_CHECK(mem.read_u64(0x4010) == 0x1122334455667788ULL, "stp pair low");
    TEST_CHECK(mem.read_u64(0x4018) == 0x1122334455667788ULL, "stp pair high");

    // LDP X3, X4, [X1, #16] (0xA9411023)
    run_instructions(cpu, mem, { 0xA9411023u });
    TEST_CHECK(cpu.read_register(3) == 0x1122334455667788ULL, "ldp x3");
    TEST_CHECK(cpu.read_register(4) == 0x1122334455667788ULL, "ldp x4");

    // STRB W0, [X1, #0] (0x39000020) and LDRB W5, [X1, #0] (0x39400025)
    cpu.write_register(0, 0xAA);
    run_instructions(cpu, mem, { 0x39000020u, 0x39400025u });
    TEST_CHECK(cpu.read_register(5) == 0xAA, "ldrb reads stored byte");
}

// -----------------------------------------------------------------------------
// 8. Atomic & Synchronization Instructions (LDADD, SWP, CAS, Barriers)
// -----------------------------------------------------------------------------
static void test_atomic_and_synchronization_instructions() {
    std::cout << "[RUN] test_atomic_and_synchronization_instructions\n";
    Memory mem(0x20000);
    CPU cpu;

    // LDADD X0, X2, [X1] (0xF8200022): Atomic Add
    // Memory[X1] += X0, returns original value in X2
    const std::uint64_t target_addr = 0x5000;
    mem.write_u64(static_cast<std::size_t>(target_addr), 100);
    cpu.write_register(0, 50); // addend
    cpu.write_register(1, target_addr);
    run_instructions(cpu, mem, { 0xF8200022u }); // ldadd x0, x2, [x1]
    TEST_CHECK(cpu.read_register(2) == 100, "ldadd returns original value 100");
    TEST_CHECK(mem.read_u64(static_cast<std::size_t>(target_addr)) == 150, "ldadd memory updated to 150");

    // SWP X0, X2, [X1] (0xF8208022): Atomic Swap
    cpu.write_register(0, 999);
    run_instructions(cpu, mem, { 0xF8208022u }); // swp x0, x2, [x1]
    TEST_CHECK(cpu.read_register(2) == 150, "swp returns prior value 150");
    TEST_CHECK(mem.read_u64(static_cast<std::size_t>(target_addr)) == 999, "swp updates memory to 999");

    // CAS X0, X2, [X1] (0xC8A07C22): Compare and Swap 64-bit
    // Compare memory against X0: if match, store X2 into memory. X0 gets old value.
    cpu.write_register(0, 999); // expected value (matches)
    cpu.write_register(2, 777); // new value
    run_instructions(cpu, mem, { 0xC8A07C22u }); // cas x0, x2, [x1]
    TEST_CHECK(mem.read_u64(static_cast<std::size_t>(target_addr)) == 777, "cas successful store");

    // Barriers: DMB ISHLD, DSB ISH, ISB
    run_instructions(cpu, mem, {
        0xD5033ABFu, // dmb ishld
        0xD503399Fu, // dsb ish
        0xD5033FDFu  // isb
    });
    TEST_CHECK(true, "barriers execute cleanly without exceptions");
}

// -----------------------------------------------------------------------------
// 9. Floating-Point & Vector Scalar Instructions (FADD, FMUL, FCMP)
// -----------------------------------------------------------------------------
static void test_floating_point_scalar_instructions() {
    std::cout << "[RUN] test_floating_point_scalar_instructions\n";
    Memory mem(0x20000);
    CPU cpu;
    cpu.set_cpacr_el1(0x00300000u); // Enable FP/SIMD access (FPEN = 0b11)

    // FADD D0, D1, D2 (0x1E622820): Double precision floating point addition
    double d1 = 3.5;
    double d2 = 2.25;
    std::uint64_t raw_d1, raw_d2;
    std::memcpy(&raw_d1, &d1, sizeof(d1));
    std::memcpy(&raw_d2, &d2, sizeof(d2));

    cpu.write_vector_register(1, raw_d1, 0);
    cpu.write_vector_register(2, raw_d2, 0);

    run_instructions(cpu, mem, { 0x1E622820u }); // fadd d0, d1, d2
    CPU::VectorRegister v0 = cpu.read_vector_register(0);
    double res_d = 0.0;
    std::memcpy(&res_d, &v0.low, sizeof(res_d));
    TEST_CHECK(std::abs(res_d - 5.75) < 1e-6, "fadd double precision result 5.75");

    // FMUL D0, D1, D2 (0x1E620820): Double precision multiply
    run_instructions(cpu, mem, { 0x1E620820u }); // fmul d0, d1, d2
    v0 = cpu.read_vector_register(0);
    std::memcpy(&res_d, &v0.low, sizeof(res_d));
    TEST_CHECK(std::abs(res_d - (3.5 * 2.25)) < 1e-6, "fmul double precision result");

    // FCMP D1, D2 (0x1E622020): Compare D1 > D2 -> C=1, Z=0
    run_instructions(cpu, mem, { 0x1E622020u }); // fcmp d1, d2
    TEST_CHECK((cpu.get_nzcv() & 0x2u) != 0, "fcmp D1 > D2 sets C flag");
    TEST_CHECK((cpu.get_nzcv() & 0x4u) == 0, "fcmp D1 != D2 clears Z flag");
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "STAGE 204.18: ARM64 Instruction Coverage & Matrix\n";
    std::cout << "Target: iPhone 15 (iPhone15,4 / D37AP / A16 T8120)\n";
    std::cout << "====================================================\n" << std::flush;

    try { test_integer_arithmetic_and_flags(); } catch (const std::exception& e) { std::cerr << "test_integer_arithmetic_and_flags threw: " << e.what() << "\n"; g_failed_tests++; }
    try { test_bitwise_logical_operations(); } catch (const std::exception& e) { std::cerr << "test_bitwise_logical_operations threw: " << e.what() << "\n"; g_failed_tests++; }
    try { test_bitfield_and_reverse_operations(); } catch (const std::exception& e) { std::cerr << "test_bitfield_and_reverse_operations threw: " << e.what() << "\n"; g_failed_tests++; }
    try { test_multiply_and_divide(); } catch (const std::exception& e) { std::cerr << "test_multiply_and_divide threw: " << e.what() << "\n"; g_failed_tests++; }
    try { test_conditional_select_operations(); } catch (const std::exception& e) { std::cerr << "test_conditional_select_operations threw: " << e.what() << "\n"; g_failed_tests++; }
    try { test_branching_and_control_flow(); } catch (const std::exception& e) { std::cerr << "test_branching_and_control_flow threw: " << e.what() << "\n"; g_failed_tests++; }
    try { test_load_store_memory_behavior(); } catch (const std::exception& e) { std::cerr << "test_load_store_memory_behavior threw: " << e.what() << "\n"; g_failed_tests++; }
    try { test_atomic_and_synchronization_instructions(); } catch (const std::exception& e) { std::cerr << "test_atomic_and_synchronization_instructions threw: " << e.what() << "\n"; g_failed_tests++; }
    try { test_floating_point_scalar_instructions(); } catch (const std::exception& e) { std::cerr << "test_floating_point_scalar_instructions threw: " << e.what() << "\n"; g_failed_tests++; }

    std::cout << "====================================================\n";
    if (g_failed_tests == 0) {
        std::cout << "[PASS] Stage 204.18: All ARM64 Instruction Coverage Tests PASSED!\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.18: " << g_failed_tests << " test(s) FAILED!\n";
        return 1;
    }
}
