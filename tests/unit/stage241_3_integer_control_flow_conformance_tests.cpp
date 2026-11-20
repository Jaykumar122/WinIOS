// ============================================================================
// Stage 241.3: Independent Integer and Control-Flow Conformance Tests
// Table-driven tests for ARM64 integer operations, boundary operands,
// W-register zero-extension, flag computations (NZCV), and branch behavior.
// ============================================================================

#include "cpu/cpu.h"
#include "memory/memory.h"

#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

struct ArithmeticTestCase {
    const char* description;
    uint32_t opcode;
    uint64_t initial_x1;
    uint64_t initial_x2;
    uint64_t expected_x0;
    uint8_t expected_nzcv_mask;
    uint8_t expected_nzcv_val;
};

static void test_table_driven_arithmetic_boundaries() {
    std::cout << "[Stage 241.3 - Test 1] Table-driven integer operations and boundary operands...\n";
    CPU cpu;
    Memory mem(4 * 1024 * 1024);
    const uint64_t pc_base = 0x10000;

    // Test cases:
    // NZCV bits: bit 3 = N (0x8), bit 2 = Z (0x4), bit 1 = C (0x2), bit 0 = V (0x1)
    std::vector<ArithmeticTestCase> cases = {
        // ADD X0, X1, X2 (0 + 0 = 0)
        { "ADD X0, X1, X2 with zeros", 0x8B020020u, 0ULL, 0ULL, 0ULL, 0x0, 0x0 },
        // ADD X0, X1, X2 (UINT64_MAX + 1 = 0 overflow)
        { "ADD X0, X1, X2 wrap around", 0x8B020020u, 0xFFFFFFFFFFFFFFFFULL, 1ULL, 0ULL, 0x0, 0x0 },
        // ADDS X0, X1, X2 (10 + -10 = 0 -> Z=1, C=1)
        { "ADDS X0, X1, X2 zero result", 0xAB020020u, 10ULL, static_cast<uint64_t>(-10LL), 0ULL, 0x4u, 0x4u },
        // ADDS X0, X1, X2 (INT64_MAX + 1 -> N=1, V=1)
        { "ADDS X0, X1, X2 signed overflow", 0xAB020020u, 0x7FFFFFFFFFFFFFFFULL, 1ULL, 0x8000000000000000ULL, 0x9u, 0x9u },
        // SUB X0, X1, X2 (50 - 20 = 30)
        { "SUB X0, X1, X2 positive result", 0xCB020020u, 50ULL, 20ULL, 30ULL, 0x0, 0x0 },
        // SUBS XZR, X1, X2 (50 - 50 = 0 -> Z=1, C=1)
        { "SUBS XZR, X1, X2 compare equal", 0xEB02003Fu, 50ULL, 50ULL, 0ULL, 0x6u, 0x6u },
        // SUBS XZR, X1, X2 (20 - 50 = -30 -> N=1, C=0)
        { "SUBS XZR, X1, X2 compare less", 0xEB02003Fu, 20ULL, 50ULL, 0ULL, 0xAu, 0x8u },
        // UDIV X0, X1, X2 (100 / 0 = 0, no trap per ARM64)
        { "UDIV X0, X1, X2 divide by zero", 0x9AC20820u, 100ULL, 0ULL, 0ULL, 0x0, 0x0 },
        // UDIV X0, X1, X2 (UINT64_MAX / 1 = UINT64_MAX)
        { "UDIV X0, X1, X2 max unsigned", 0x9AC20820u, 0xFFFFFFFFFFFFFFFFULL, 1ULL, 0xFFFFFFFFFFFFFFFFULL, 0x0, 0x0 },
    };

    for (size_t i = 0; i < cases.size(); ++i) {
        const auto& tc = cases[i];
        cpu.reset();
        cpu.write_register(1, tc.initial_x1);
        cpu.write_register(2, tc.initial_x2);
        cpu.set_program_counter(pc_base);

        mem.write_u32(pc_base, tc.opcode);
        cpu.step(mem);

        if (tc.expected_x0 != 0 || tc.opcode != 0xEB02003Fu) {
            TEST_ASSERT(cpu.read_register(0) == tc.expected_x0, tc.description);
        }
        if (tc.expected_nzcv_mask != 0) {
            const uint8_t actual_nzcv = cpu.get_nzcv() & tc.expected_nzcv_mask;
            TEST_ASSERT(actual_nzcv == tc.expected_nzcv_val, tc.description);
        }
    }
}

static void test_w_register_zero_extension() {
    std::cout << "[Stage 241.3 - Test 2] W-register 32-bit zero-extension semantics...\n";
    CPU cpu;
    Memory mem(4 * 1024 * 1024);
    const uint64_t pc = 0x20000;

    // ADD W0, W1, W2: 32-bit addition (0x0B020020u)
    // Initialize X1 with upper 32-bits dirty: 0xFFFFFFFF00000020
    // Initialize X2 with upper 32-bits dirty: 0xAAAAAAAA00000010
    // Result W0 must be 0x30, and upper 32 bits of X0 must be ZERO!
    cpu.reset();
    cpu.write_register(0, 0xDEADBEEFCAFEBABEULL);
    cpu.write_register(1, 0xFFFFFFFF00000020ULL);
    cpu.write_register(2, 0xAAAAAAAA00000010ULL);

    mem.write_u32(pc, 0x0B020020u); // ADD W0, W1, W2
    cpu.set_program_counter(pc);
    cpu.step(mem);

    const uint64_t x0_val = cpu.read_register(0);
    TEST_ASSERT((x0_val >> 32) == 0ULL, "Upper 32 bits of X0 must be cleared to 0 after W-register operation");
    TEST_ASSERT(x0_val == 0x30ULL, "Lower 32 bits must equal 0x30");
}

static void test_control_flow_and_link_register() {
    std::cout << "[Stage 241.3 - Test 3] Control flow, BL link-register update, and RET...\n";
    CPU cpu;
    Memory mem(4 * 1024 * 1024);
    const uint64_t pc = 0x30000;

    // BL +16 (offset = 4 instructions)
    // 0x94000004u (BL .+16)
    cpu.reset();
    mem.write_u32(pc, 0x94000004u);
    // Target instruction at pc + 16: RET (0xD65F03C0u)
    mem.write_u32(pc + 16, 0xD65F03C0u);

    cpu.set_program_counter(pc);
    cpu.step(mem); // Execute BL
    TEST_ASSERT(cpu.get_program_counter() == pc + 16, "BL must branch to target");
    TEST_ASSERT(cpu.read_register(30) == pc + 4, "BL must save return address PC+4 into X30");

    cpu.step(mem); // Execute RET
    TEST_ASSERT(cpu.get_program_counter() == pc + 4, "RET must restore PC from X30");
}

int main() {
    std::cout << "==========================================================\n";
    std::cout << "Stage 241.3: ARM64 Integer & Control-Flow Conformance Suite\n";
    std::cout << "==========================================================\n";

    test_table_driven_arithmetic_boundaries();
    test_w_register_zero_extension();
    test_control_flow_and_link_register();

    std::cout << "[PASS] Stage 241.3 Conformance tests completed successfully.\n";
    return 0;
}
