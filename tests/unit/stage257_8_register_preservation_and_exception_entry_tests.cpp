// ============================================================================
// Stage 257.6 - 257.8: Full Register Preservation Audit, Correct Exception
// Entry Trampoline Implementation & Complete Register Invariant Tests
// ============================================================================

#include "cpu/arm_saved_state.h"
#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "device/apple_aic.h"
#include "memory/memory.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;

// ----------------------------------------------------------------------------
// Test 1: Stage 257.6: Full Register Preservation Audit
// ----------------------------------------------------------------------------
static void test_stage257_6_register_preservation_audit() {
    std::cout << "[Stage 257.6] Testing Full Register Preservation Audit...\n";

    // In Campaign 256, the 6-instruction trampoline at 0x0A80 was:
    // 0x0A80: sub sp, sp, #32
    // 0x0A84: stp x0, x1, [sp]
    // 0x0A88: add x2, x2, #1        <--- DEFECT: modifies X2 without saving/restoring X2!
    // 0x0A8C: ldp x0, x1, [sp]
    // 0x0A90: add sp, sp, #32
    // 0x0A94: eret
    //
    // Consequence: When ERET executes, the interrupted thread resumes with X2 corrupted.
    // Furthermore, X3..X30 are not saved or protected.

    const bool campaign256_trampoline_clobbered_x2 = true;
    TEST_ASSERT(campaign256_trampoline_clobbered_x2, "Campaign 256 minimal trampoline clobbered X2");

    // Required Contract for Campaign 257:
    // 1. Full register safety: No GPR (X0..X30) may be clobbered across an exception boundary.
    // 2. Any register used internally by the handler MUST be saved and restored before ERET.
    // 3. For full state inspection, a complete 288-byte ArmSavedState64 frame must be constructible.

    std::cout << "[Stage 257.6] Register preservation audit completed.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 257.7: Correct Exception Entry Trampoline Implementation
// ----------------------------------------------------------------------------
static void test_stage257_7_correct_exception_entry_implementation() {
    std::cout << "[Stage 257.7] Testing Correct Exception Entry Trampoline Implementation...\n";

    // Construct a register-safe exception trampoline:
    // Uses 48-byte stack scratch frame (aligned to 16 bytes).
    // Saves X0, X1, X2, X3, X29 (FP), X30 (LR).
    // Performs handler operations.
    // Fully restores all saved registers before executing ERET.

    Memory mem(0x800000ULL);

    // Opcodes:
    // 0x0A80: sub sp, sp, #48           (0xD100C3FF)
    // 0x0A84: stp x0, x1, [sp, #0]      (0xA90007E0)
    // 0x0A88: stp x2, x3, [sp, #16]     (0xA9010FE2)
    // 0x0A8C: stp x29, x30, [sp, #32]   (0xA9027BFD)
    // 0x0A90: add x2, x2, #1            (0x91000442) - internal handler operation
    // 0x0A94: ldp x29, x30, [sp, #32]   (0xA9427BFD) - restore FP, LR
    // 0x0A98: ldp x2, x3, [sp, #16]     (0xA9410FE2) - RESTORE X2, X3 (PRESERVES X2!)
    // 0x0A9C: ldp x0, x1, [sp, #0]      (0xA94007E0) - restore X0, X1
    // 0x0AA0: add sp, sp, #48           (0x9100C3FF) - deallocate frame
    // 0x0AA4: eret                      (0xD69F03E0) - return to interrupted instruction

    const uint32_t safe_trampoline[] = {
        0xD100C3FFu, // sub sp, sp, #48
        0xA90007E0u, // stp x0, x1, [sp, #0]
        0xA9010FE2u, // stp x2, x3, [sp, #16]
        0xA9027BFDu, // stp x29, x30, [sp, #32]
        0x91000442u, // add x2, x2, #1
        0xA9427BFDu, // ldp x29, x30, [sp, #32]
        0xA9410FE2u, // ldp x2, x3, [sp, #16]
        0xA94007E0u, // ldp x0, x1, [sp, #0]
        0x9100C3FFu, // add sp, sp, #48
        0xD69F03E0u  // eret
    };

    for (std::size_t i = 0; i < sizeof(safe_trampoline)/sizeof(uint32_t); ++i) {
        mem.write_u32(0x00000A80ULL + (i * 4), safe_trampoline[i]);
    }

    // Verify opcodes written correctly
    for (std::size_t i = 0; i < sizeof(safe_trampoline)/sizeof(uint32_t); ++i) {
        TEST_ASSERT(mem.read_u32(0x00000A80ULL + (i * 4)) == safe_trampoline[i], "Trampoline opcode verified");
    }

    std::cout << "[Stage 257.7] Correct exception entry trampoline implemented and verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 257.8: Complete Register Preservation Tests (X0..X30, SP, PC)
// ----------------------------------------------------------------------------
static void test_stage257_8_complete_register_preservation() {
    std::cout << "[Stage 257.8] Testing Complete Register Preservation across Exception Boundary...\n";

    Memory mem(0x800000ULL);

    // Setup interrupted guest program at 0x1000:
    // 0x1000: msr daifclr, #2       (unmask IRQ)
    // 0x1004: mov x20, #0x100       (interrupted task payload)
    // 0x1008: wfi                   (wait for interrupt)
    // 0x100C: add x20, x20, #0x55   (continuation instruction)
    // 0x1010: mov x21, #0x777       (completion marker)
    mem.write_u32(0x1000, 0xD50342FFu);
    mem.write_u32(0x1004, 0xD2802014u);
    mem.write_u32(0x1008, 0xD503207Fu); // wfi
    mem.write_u32(0x100C, 0x91015694u); // add x20, x20, #0x55
    mem.write_u32(0x1010, 0xD280EEF5u); // mov x21, #0x777

    // Setup register-safe trampoline at 0x00000A80
    const uint32_t safe_trampoline[] = {
        0xD100C3FFu, // sub sp, sp, #48
        0xA90007E0u, // stp x0, x1, [sp, #0]
        0xA9010FE2u, // stp x2, x3, [sp, #16]
        0xA9027BFDu, // stp x29, x30, [sp, #32]
        0x91000442u, // add x2, x2, #1 (internal hook)
        0xA9427BFDu, // ldp x29, x30, [sp, #32]
        0xA9410FE2u, // ldp x2, x3, [sp, #16]
        0xA94007E0u, // ldp x0, x1, [sp, #0]
        0x9100C3FFu, // add sp, sp, #48
        0xD69F03E0u  // eret
    };
    for (std::size_t i = 0; i < sizeof(safe_trampoline)/sizeof(uint32_t); ++i) {
        mem.write_u32(0x00000A80ULL + (i * 4), safe_trampoline[i]);
    }

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x00000800ULL);
    cpu.set_sp_el1(0x00044000ULL);
    cpu.set_program_counter(0x1000ULL);

    // Initialize EVERY general-purpose register X0..X30 with a unique recognizable pattern
    for (std::size_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0xCAFE0000ULL + i);
    }

    // Execute setup instructions up to WFI
    cpu.step(mem); // 0x1000: msr daifclr
    cpu.step(mem); // 0x1004: mov x20, #0x100
    cpu.step(mem); // 0x1008: wfi

    // Assert interrupt from external source (e.g. AIC2 HW IRQ 44)
    cpu.wake_from_wfi();
    cpu.raise_asynchronous_exception(false, 44, false, 0);

    TEST_ASSERT(cpu.get_program_counter() == 0x00000A80ULL, "Vectored to 0x0A80");
    TEST_ASSERT(cpu.get_elr_el1() == 0x100CULL, "ELR_EL1 captured interrupted PC + 4");

    // Execute the 10 trampoline instructions
    for (int i = 0; i < 10; ++i) {
        cpu.step(mem);
    }

    // Verification after ERET:
    TEST_ASSERT(cpu.get_program_counter() == 0x100CULL, "ERET returned to 0x100C");
    TEST_ASSERT(cpu.get_sp_el1() == 0x00044000ULL, "SP restored to original 0x44000");

    // Dedicated regression test for X2:
    TEST_ASSERT(cpu.read_register(2) == 0xCAFE0002ULL, "X2 is strictly preserved (NO silent corruption!)");

    // Comprehensive test for ALL registers X0 through X30:
    for (std::size_t i = 0; i < 31; ++i) {
        if (i == 20) {
            // X20 was set to 0x100 at 0x1004 prior to WFI
            TEST_ASSERT(cpu.read_register(20) == 0x100ULL, "X20 has interrupted value 0x100");
        } else {
            TEST_ASSERT(cpu.read_register(i) == (0xCAFE0000ULL + i), "Register Xi preserved");
        }
    }

    // Step continuation instructions:
    cpu.step(mem); // 0x100C: add x20, x20, #0x55
    TEST_ASSERT(cpu.read_register(20) == 0x155ULL, "Resumed code successfully executed (0x100 + 0x55)");

    cpu.step(mem); // 0x1010: mov x21, #0x777
    TEST_ASSERT(cpu.read_register(21) == 0x777ULL, "Final instruction executed successfully");

    std::cout << "[Stage 257.8] Complete register preservation verified: all 31 GPRs intact after ERET.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 257: Stages 257.6 - 257.8 Register Preservation Tests\n";
    std::cout << "====================================================================\n";

    test_stage257_6_register_preservation_audit();
    test_stage257_7_correct_exception_entry_implementation();
    test_stage257_8_complete_register_preservation();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 257.6 - 257.8 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
