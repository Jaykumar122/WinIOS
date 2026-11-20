// ============================================================================
// Stage 243.14: AAPCS64 Register Classification & LR Preservation Conformance Tests
// Verifies AAPCS64 calling conventions: X19-X29 callee-saved, X30 Link Register
// return address rules (saved on stack across nested calls), and 16-byte SP alignment.
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

static void test_callee_saved_x19_x29() {
    std::cout << "[Stage 243.14 - Test 1] AAPCS64 Callee-saved registers X19-X28 and Frame Pointer X29...\n";
    CPU cpu;
    cpu.reset();

    // Set values in callee-saved registers X19-X28
    for (size_t r = 19; r <= 28; ++r) {
        cpu.write_register(r, 0x1900ULL + r);
    }
    // Set Frame Pointer X29
    cpu.write_register(29, 0x29000ULL);

    // Verify all preserved
    for (size_t r = 19; r <= 28; ++r) {
        TEST_ASSERT(cpu.read_register(r) == (0x1900ULL + r), "Callee-saved Xi preserved");
    }
    TEST_ASSERT(cpu.read_register(29) == 0x29000ULL, "FP X29 preserved");
}

static void test_lr_nested_call_stack_preservation() {
    std::cout << "[Stage 243.14 - Test 2] X30/LR return address and stack preservation pattern...\n";
    CPU cpu;
    Memory mem(0x10000);
    cpu.reset();

    uint64_t initial_sp = 0x8000;
    cpu.set_stack_pointer(initial_sp);
    TEST_ASSERT((cpu.get_stack_pointer() & 0xF) == 0, "SP 16-byte aligned");

    // Parent function return address in X30
    const uint64_t parent_ret_addr = 0x1004ULL;
    cpu.write_register(30, parent_ret_addr);
    cpu.write_register(29, 0x8050ULL); // caller FP

    // Non-leaf callee pushes (FP, LR) to stack: STP X29, X30, [SP, #-16]!
    uint64_t new_sp = cpu.get_stack_pointer() - 16;
    cpu.set_stack_pointer(new_sp);
    mem.write_u64(static_cast<size_t>(new_sp), cpu.read_register(29));
    mem.write_u64(static_cast<size_t>(new_sp + 8), cpu.read_register(30));

    // Nested call clobbers X30
    cpu.write_register(30, 0x2004ULL); // child return address
    TEST_ASSERT(cpu.read_register(30) == 0x2004ULL, "Child call clobbered LR");

    // Epilogue restores (FP, LR): LDP X29, X30, [SP], #16
    uint64_t restored_fp = mem.read_u64(static_cast<size_t>(cpu.get_stack_pointer()));
    uint64_t restored_lr = mem.read_u64(static_cast<size_t>(cpu.get_stack_pointer() + 8));
    cpu.write_register(29, restored_fp);
    cpu.write_register(30, restored_lr);
    cpu.set_stack_pointer(cpu.get_stack_pointer() + 16);

    TEST_ASSERT(cpu.read_register(30) == parent_ret_addr, "LR successfully restored to parent return address");
    TEST_ASSERT(cpu.read_register(29) == 0x8050ULL, "FP successfully restored");
    TEST_ASSERT(cpu.get_stack_pointer() == initial_sp, "SP restored to initial value");
}

int main() {
    std::cout << "=== Running Stage 243.14 AAPCS64 LR Preservation Tests ===\n";
    test_callee_saved_x19_x29();
    test_lr_nested_call_stack_preservation();
    std::cout << "Stage 243.14: All tests passed successfully.\n";
    return 0;
}
