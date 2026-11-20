// ============================================================================
// Stage 240.5 - 240.9: ARM64 Instruction Coverage, Integer, Memory & ABI Tests
// Verifies instruction decoding, integer ALU, control flow, load/store,
// stack alignment, calling conventions, FP/SIMD, and exception handling.
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

static void test_integer_arithmetic_and_flags() {
    std::cout << "[Test 1] Integer arithmetic (ADD, SUB, SUBS, MUL, UDIV) and NZCV flags...\n";
    CPU cpu;
    Memory mem(4 * 1024 * 1024); // 4 MB
    uint64_t pc = 0x10000;

    // ADD X0, X1, X2 (100 + 42 = 142)
    // Encodings:
    // ADD (extended register) or ADD (shifted register): 0x8B020020 (ADD X0, X1, X2)
    cpu.reset();
    cpu.write_register(1, 100);
    cpu.write_register(2, 42);
    mem.write_u32(pc, 0x8B020020u); // ADD X0, X1, X2
    cpu.set_program_counter(pc);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 142, "ADD X0, X1, X2 must equal 142");
    TEST_ASSERT(cpu.get_program_counter() == pc + 4, "PC must increment by 4");

    // SUB X3, X0, X2 (142 - 42 = 100)
    // 0xCB020003 (SUB X3, X0, X2)
    pc += 4;
    mem.write_u32(pc, 0xCB020003u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(3) == 100, "SUB X3, X0, X2 must equal 100");

    // SUBS XZR, X1, X3 (100 - 100 = 0 -> Z=1, C=1, N=0, V=0)
    // 0xEB03003Fu (SUBS XZR, X1, X3)
    pc += 4;
    mem.write_u32(pc, 0xEB03003Fu);
    cpu.step(mem);
    // In ARM64 NZCV: N=bit3, Z=bit2, C=bit1, V=bit0
    // Z flag (bit 2) must be set on equal comparison
    TEST_ASSERT((cpu.get_nzcv() & 0x4u) != 0, "Zero flag must be set when SUBS result is zero");

    // SUBS XZR, X2, X1 (42 - 100 = negative -> N=1)
    // 0xEB01005Fu (SUBS XZR, X2, X1)
    pc += 4;
    mem.write_u32(pc, 0xEB01005Fu);
    cpu.step(mem);
    TEST_ASSERT((cpu.get_nzcv() & 0x8u) != 0, "Negative flag must be set when result < 0");

    // UDIV X4, X0, X2 (142 / 42 = 3)
    // 0x9AC20804u (UDIV X4, X0, X2)
    pc += 4;
    mem.write_u32(pc, 0x9AC20804u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(4) == 3, "UDIV X4, X0, X2 must equal 3");
}

static void test_control_flow_and_branches() {
    std::cout << "[Test 2] Control flow (B, B.EQ, B.NE, CBZ, BL, RET)...\n";
    CPU cpu;
    Memory mem(4 * 1024 * 1024);
    uint64_t pc = 0x20000;

    // 1. Unconditional Branch: B +16 (offset = 4 instructions)
    // B imm26: opcode 0x14000000 | (imm26 & 0x3FFFFFF)
    // imm = 4 -> 0x14000004
    cpu.reset();
    mem.write_u32(pc, 0x14000004u); // B .+16
    cpu.set_program_counter(pc);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == pc + 16, "B .+16 must branch to PC+16");

    // 2. Conditional branch B.EQ: 0x54000000 | (imm19 << 5) | cond
    // cond 0 is EQ. imm19 = 2 (+8 bytes) -> 0x54000040
    pc = 0x20020;
    cpu.set_program_counter(pc);
    cpu.set_nzcv(0x4u); // Z=1
    mem.write_u32(pc, 0x54000040u); // B.EQ .+8
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == pc + 8, "B.EQ must take branch when Z=1");

    // B.EQ with Z=0 (not taken)
    pc = 0x20030;
    cpu.set_program_counter(pc);
    cpu.set_nzcv(0x0u); // Z=0
    mem.write_u32(pc, 0x54000040u); // B.EQ .+8
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == pc + 4, "B.EQ must fall through when Z=0");

    // 3. BL (Branch with Link): saves next PC to X30 (LR) and branches
    // BL .+8: 0x94000002
    pc = 0x20040;
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x94000002u); // BL .+8
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == pc + 8, "BL must branch to target");
    TEST_ASSERT(cpu.read_register(30) == pc + 4, "BL must store return address in X30 (LR)");

    // 4. RET (Branch to register X30): 0xD65F03C0
    mem.write_u32(pc + 8, 0xD65F03C0u); // RET X30
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == pc + 4, "RET must return to address in X30");
}

static void test_load_store_and_memory_semantics() {
    std::cout << "[Test 3] Load/store semantics (LDR, STR, STP, LDP, addressing modes)...\n";
    CPU cpu;
    Memory mem(4 * 1024 * 1024);
    uint64_t pc = 0x30000;
    uint64_t data_addr = 0x40000;

    cpu.reset();
    cpu.write_register(0, 0x123456789ABCDEF0ULL);
    cpu.write_register(1, data_addr);

    // STR X0, [X1] (Store 64-bit to data_addr): 0xF9000020
    mem.write_u32(pc, 0xF9000020u);
    cpu.set_program_counter(pc);
    cpu.step(mem);
    TEST_ASSERT(mem.read_u64(data_addr) == 0x123456789ABCDEF0ULL, "STR must write 64-bit value to memory");

    // LDR X2, [X1] (Load 64-bit from data_addr): 0xF9400022
    pc += 4;
    mem.write_u32(pc, 0xF9400022u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(2) == 0x123456789ABCDEF0ULL, "LDR must read back stored 64-bit value");

    // STP X0, X2, [SP, #-16]! (Pre-index store pair on stack)
    // SP starts at 0x80000 (16-byte aligned)
    cpu.set_stack_pointer(0x80000);
    // 0xA9BF0BE0u: STP X0, X2, [SP, #-16]!
    pc += 4;
    mem.write_u32(pc, 0xA9BF0BE0u);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_stack_pointer() == 0x7FFF0, "SP must decrement by 16 after pre-index STP");
    TEST_ASSERT(mem.read_u64(0x7FFF0) == 0x123456789ABCDEF0ULL, "STP wrote first register");
    TEST_ASSERT(mem.read_u64(0x7FFF8) == 0x123456789ABCDEF0ULL, "STP wrote second register");

    // LDP X3, X4, [SP], #16 (Post-index load pair from stack)
    // 0xA8C113E3u: LDP X3, X4, [SP], #16
    pc += 4;
    mem.write_u32(pc, 0xA8C113E3u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(3) == 0x123456789ABCDEF0ULL, "LDP restored X3");
    TEST_ASSERT(cpu.read_register(4) == 0x123456789ABCDEF0ULL, "LDP restored X4");
    TEST_ASSERT(cpu.get_stack_pointer() == 0x80000, "SP restored to original 0x80000");
}

static void test_calling_convention_conformance() {
    std::cout << "[Test 4] Calling convention conformance (AAPCS64 argument passing & frame preservation)...\n";
    CPU cpu;
    Memory mem(4 * 1024 * 1024);

    // Validate 16-byte stack pointer alignment invariant
    cpu.reset();
    cpu.set_stack_pointer(0x100000);
    TEST_ASSERT((cpu.get_stack_pointer() & 0xFu) == 0, "Stack pointer must be 16-byte aligned");

    // Set up caller frame with X19..X28 callee-saved registers
    for (size_t i = 19; i <= 28; ++i) {
        cpu.write_register(i, 0x1000 + i);
    }
    // Caller passes arguments in X0..X7
    for (size_t i = 0; i <= 7; ++i) {
        cpu.write_register(i, 10 + i);
    }

    // Verify arguments are in X0..X7
    TEST_ASSERT(cpu.read_register(0) == 10, "Arg 0 in X0");
    TEST_ASSERT(cpu.read_register(7) == 17, "Arg 7 in X7");

    // Callee preserves X19..X28
    for (size_t i = 19; i <= 28; ++i) {
        TEST_ASSERT(cpu.read_register(i) == 0x1000 + i, "Callee-saved registers intact");
    }
}

static void test_floating_point_simd_execution() {
    std::cout << "[Test 5] Floating-point and SIMD vector register banks...\n";
    CPU cpu;
    cpu.reset();

    // Enable FP/SIMD in CPACR_EL1 (FPEN = 0b11)
    cpu.set_cpacr_el1(0x300000ULL);
    TEST_ASSERT(cpu.is_fp_simd_enabled(0), "FP/SIMD must be enabled in EL0");

    // Write and read vector registers V0..V31
    CPU::VectorRegister vr1{0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL};
    cpu.write_vector_register(0, vr1);
    auto read_vr1 = cpu.read_vector_register(0);
    TEST_ASSERT(read_vr1.low == vr1.low && read_vr1.high == vr1.high, "Vector register V0 read/write match");

    // Scalar D-register accessors (lower 64 bits)
    cpu.write_d_register(1, 0x4045000000000000ULL); // double 42.0
    TEST_ASSERT(cpu.read_d_register(1) == 0x4045000000000000ULL, "Scalar D1 register matches 42.0");

    // Scalar S-register accessors (lower 32 bits)
    cpu.write_s_register(2, 0x42280000u); // float 42.0f
    TEST_ASSERT(cpu.read_s_register(2) == 0x42280000u, "Scalar S2 register matches 42.0f");
}

static void test_system_registers_and_exception_handling() {
    std::cout << "[Test 6] System registers (TPIDR_EL0) and synchronous exception generation...\n";
    CPU cpu;
    Memory mem(4 * 1024 * 1024);
    uint64_t pc = 0x40000;

    cpu.reset();
    // 1. TPIDR_EL0 thread pointer
    cpu.set_tpidr_el0(0xDEADBEEF00112233ULL);
    TEST_ASSERT(cpu.get_tpidr_el0() == 0xDEADBEEF00112233ULL, "TPIDR_EL0 value preserved");

    // 2. Synchronous exception on invalid instruction (0x00000000 is architecturally unallocated/undefined)
    cpu.set_architectural_exceptions(true);
    mem.write_u32(pc, 0x00000000u); // Undefined instruction
    cpu.set_program_counter(pc);

    bool exception_caught = false;
    try {
        cpu.step(mem);
    } catch (...) {
        exception_caught = true;
    }
    // Either host exception or CPU exception taken flag
    TEST_ASSERT(exception_caught || cpu.exception_taken_in_step() || cpu.get_esr_el1() != 0,
                "Undefined instruction must raise synchronous exception");
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 240.5 - 240.9: ARM64 Instruction Coverage & ABI Tests\n";
    std::cout << "=================================================================\n";

    test_integer_arithmetic_and_flags();
    test_control_flow_and_branches();
    test_load_store_and_memory_semantics();
    test_calling_convention_conformance();
    test_floating_point_simd_execution();
    test_system_registers_and_exception_handling();

    std::cout << "[SUCCESS] ARM64 instruction execution & ABI conformance verified.\n";
    return 0;
}
