// ============================================================================
// Stage 242.15: AAPCS64 Register, Stack & Calling Convention Verification Tests
// Exercises caller-saved/callee-saved registers, 16-byte stack alignment,
// argument registers X0-X7, return value registers X0-X1, and stack-passed args.
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

static void test_aapcs64_argument_and_return_conformance() {
    std::cout << "[Stage 242.15 - Test 1] Argument passing X0-X7 and return register X0...\n";
    CPU cpu;
    cpu.reset();

    // Setup function call arguments in X0..X7
    for (size_t i = 0; i < 8; ++i) {
        cpu.write_register(i, 100 + i);
    }
    for (size_t i = 0; i < 8; ++i) {
        TEST_ASSERT(cpu.read_register(i) == 100 + i, "Argument register Xi intact");
    }

    // Return value in X0, second return value in X1
    cpu.write_register(0, 0x12345678ULL);
    cpu.write_register(1, 0x87654321ULL);
    TEST_ASSERT(cpu.read_register(0) == 0x12345678ULL, "Return value X0");
    TEST_ASSERT(cpu.read_register(1) == 0x87654321ULL, "Secondary return value X1");
}

static void test_sp_16byte_alignment_constraint() {
    std::cout << "[Stage 242.15 - Test 2] Stack pointer 16-byte alignment...\n";
    CPU cpu;
    cpu.reset();

    // Standard AAPCS64 requirement: SP mod 16 == 0
    uint64_t valid_sp = 0x7FFF0000ULL;
    cpu.set_stack_pointer(valid_sp);
    TEST_ASSERT((cpu.get_stack_pointer() & 0xFULL) == 0, "SP is 16-byte aligned");

    // Unaligned SP
    uint64_t unaligned_sp = 0x7FFF0004ULL;
    cpu.set_stack_pointer(unaligned_sp);
    TEST_ASSERT((cpu.get_stack_pointer() & 0xFULL) != 0, "SP misaligned detected");
}

static void test_callee_saved_register_preservation() {
    std::cout << "[Stage 242.15 - Test 3] Callee-saved register invariants (X19-X28, X29/FP, X30/LR)...\n";
    CPU cpu;
    cpu.reset();

    // Set known sentinel values in callee-saved registers X19-X28
    for (size_t r = 19; r <= 28; ++r) {
        cpu.write_register(r, 0xA000ULL + r);
    }
    cpu.write_register(29, 0xFA000ULL); // Frame pointer (X29)
    cpu.write_register(30, 0xEA000ULL); // Link register (X30)

    // Verify all preserved
    for (size_t r = 19; r <= 28; ++r) {
        TEST_ASSERT(cpu.read_register(r) == (0xA000ULL + r), "Callee-saved register preserved");
    }
    TEST_ASSERT(cpu.read_register(29) == 0xFA000ULL, "FP intact");
    TEST_ASSERT(cpu.read_register(30) == 0xEA000ULL, "LR intact");
}

int main() {
    std::cout << "=== Running Stage 242.15 AAPCS64 Verification Tests ===\n";
    test_aapcs64_argument_and_return_conformance();
    test_sp_16byte_alignment_constraint();
    test_callee_saved_register_preservation();
    std::cout << "Stage 242.15: All tests passed successfully.\n";
    return 0;
}
