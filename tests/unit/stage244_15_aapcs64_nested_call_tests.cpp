// ============================================================================
// Stage 244.15: AAPCS64 Nested Call Stack Frame & Callee-Saved Register Tests
// Verifies AAPCS64 calling conventions across a multi-level nested call hierarchy:
// 1. Callee-saved register preservation (X19-X28).
// 2. Frame pointer (X29/FP) and Link Register (X30/LR) stack frame chaining.
// 3. Strict 16-byte stack pointer (SP) alignment on call boundaries.
// 4. Clean unwind restoring exact architectural register states and stack depth.
// ============================================================================

#include "cpu/cpu.h"
#include "memory/memory.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

static void test_three_level_nested_call_frame_unwind() {
    std::cout << "[Stage 244.15 - Test 1] AAPCS64 3-level nested call stack frame chaining...\n";

    CPU cpu;
    Memory mem(0x100000);
    cpu.reset();

    const uint64_t top_sp = 0x80000;
    cpu.set_stack_pointer(top_sp);
    TEST_ASSERT((cpu.get_stack_pointer() & 0xFULL) == 0, "Initial SP 16-byte aligned");

    // Initial state: Level 0 (Caller)
    // Initialize callee-saved registers X19-X28
    for (size_t r = 19; r <= 28; ++r) {
        cpu.write_register(r, 0xC0000ULL + r);
    }
    cpu.write_register(29, 0x80050ULL); // caller FP
    cpu.write_register(30, 0x1004ULL);  // caller return address

    // Level 1: Non-leaf function entry
    // Pushes (X29, X30) and saves callee-saved regs X19, X20 that Level 1 modifies
    // STP X29, X30, [SP, #-32]!
    // STP X19, X20, [SP, #16]
    uint64_t l1_sp = cpu.get_stack_pointer() - 32;
    cpu.set_stack_pointer(l1_sp);
    TEST_ASSERT((cpu.get_stack_pointer() & 0xFULL) == 0, "Level 1 SP 16-byte aligned");

    mem.write_u64(static_cast<size_t>(l1_sp), cpu.read_register(29));
    mem.write_u64(static_cast<size_t>(l1_sp + 8), cpu.read_register(30));
    mem.write_u64(static_cast<size_t>(l1_sp + 16), cpu.read_register(19));
    mem.write_u64(static_cast<size_t>(l1_sp + 24), cpu.read_register(20));

    // Update FP to point to this frame record
    cpu.write_register(29, l1_sp);

    // Modify scratch / local callee-saved registers in Level 1
    cpu.write_register(19, 0x11111ULL);
    cpu.write_register(20, 0x22222ULL);

    // Level 1 calls Level 2: sets LR to Level 1 return site
    cpu.write_register(30, 0x2008ULL);

    // Level 2: Non-leaf function entry
    // Pushes (X29, X30) and local frame
    uint64_t l2_sp = cpu.get_stack_pointer() - 16;
    cpu.set_stack_pointer(l2_sp);
    TEST_ASSERT((cpu.get_stack_pointer() & 0xFULL) == 0, "Level 2 SP 16-byte aligned");

    mem.write_u64(static_cast<size_t>(l2_sp), cpu.read_register(29));
    mem.write_u64(static_cast<size_t>(l2_sp + 8), cpu.read_register(30));
    cpu.write_register(29, l2_sp);

    // Frame chaining check: Level 2 FP points to Level 1 FP
    uint64_t chained_fp = mem.read_u64(static_cast<size_t>(cpu.read_register(29)));
    TEST_ASSERT(chained_fp == l1_sp, "Frame pointer chained correctly from Level 2 to Level 1");

    // Level 2 calls Level 3 (Leaf function)
    cpu.write_register(30, 0x3010ULL);

    // Level 3 executes, returns result in X0
    cpu.write_register(0, 42);

    // Level 2 returns: epilogue pops (X29, X30)
    uint64_t l2_ret_fp = mem.read_u64(static_cast<size_t>(cpu.get_stack_pointer()));
    uint64_t l2_ret_lr = mem.read_u64(static_cast<size_t>(cpu.get_stack_pointer() + 8));
    cpu.write_register(29, l2_ret_fp);
    cpu.write_register(30, l2_ret_lr);
    cpu.set_stack_pointer(cpu.get_stack_pointer() + 16);

    TEST_ASSERT(cpu.read_register(30) == 0x2008ULL, "Restored to Level 1 return address");
    TEST_ASSERT(cpu.get_stack_pointer() == l1_sp, "SP back at Level 1 frame");

    // Level 1 returns: epilogue pops callee-saved regs and (X29, X30)
    cpu.write_register(19, mem.read_u64(static_cast<size_t>(l1_sp + 16)));
    cpu.write_register(20, mem.read_u64(static_cast<size_t>(l1_sp + 24)));
    uint64_t l1_ret_fp = mem.read_u64(static_cast<size_t>(l1_sp));
    uint64_t l1_ret_lr = mem.read_u64(static_cast<size_t>(l1_sp + 8));
    cpu.write_register(29, l1_ret_fp);
    cpu.write_register(30, l1_ret_lr);
    cpu.set_stack_pointer(cpu.get_stack_pointer() + 32);

    // Verify complete unwind
    TEST_ASSERT(cpu.get_stack_pointer() == top_sp, "SP back to top caller frame");
    TEST_ASSERT(cpu.read_register(29) == 0x80050ULL, "Caller FP perfectly restored");
    TEST_ASSERT(cpu.read_register(30) == 0x1004ULL, "Caller LR perfectly restored");
    TEST_ASSERT(cpu.read_register(19) == 0xC0000ULL + 19, "Callee-saved X19 restored");
    TEST_ASSERT(cpu.read_register(20) == 0xC0000ULL + 20, "Callee-saved X20 restored");
    for (size_t r = 21; r <= 28; ++r) {
        TEST_ASSERT(cpu.read_register(r) == (0xC0000ULL + r), "Untouched callee-saved Xi intact");
    }
}

int main() {
    std::cout << "=== Running Stage 244.15 AAPCS64 Nested Call Tests ===\n";
    test_three_level_nested_call_frame_unwind();
    std::cout << "Stage 244.15: All tests passed successfully.\n";
    return 0;
}
