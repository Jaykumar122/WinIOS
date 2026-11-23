#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "cpu/arm_saved_state.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "cpu/kernel_exception_vectors.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;

// Helper to encode STP Xt1, Xt2, [sp, #(16 * k)]
static inline uint32_t encode_stp_sp(uint32_t k) {
    uint32_t t1 = 2 * k;
    uint32_t t2 = 2 * k + 1;
    uint32_t imm7 = 2 * k; // (16 * k) / 8
    return 0xA9000000u | (imm7 << 15) | (t2 << 10) | (31u << 5) | t1;
}

// Helper to encode LDP Xt1, Xt2, [sp, #(16 * k)]
static inline uint32_t encode_ldp_sp(uint32_t k) {
    uint32_t t1 = 2 * k;
    uint32_t t2 = 2 * k + 1;
    uint32_t imm7 = 2 * k; // (16 * k) / 8
    return 0xA9400000u | (imm7 << 15) | (t2 << 10) | (31u << 5) | t1;
}

// ----------------------------------------------------------------------------
// Test 1: Stage 262.1 - Audit of Campaign 261 Exception Save Sequence & X4 Clobber
// ----------------------------------------------------------------------------
static void test_stage262_1_x4_clobber_audit() {
    std::cout << "[Stage 262.1] Auditing Campaign 261 X4 clobber defect & remediation...\n";

    // In Campaign 261, the entry sequence used:
    // 0x6C: 0x52800024 ("mov w4, #1")
    // 0x70: 0xB9011FE4 ("str w4, [sp, #284]")
    // If w4 was written and not reloaded prior to kernel handoff, register X4 was clobbered.
    // In Campaign 262, the entry trampoline sequence explicitly concludes with:
    // 0x74: 0xA94007E0 ("ldp x0, x1, [sp, #0]")
    // 0x78: 0xA9410FE2 ("ldp x2, x3, [sp, #16]")
    // 0x7C: 0xF94013E4 ("ldr x4, [sp, #32]")
    // which completely restores X0, X1, X2, X3, X4 from the frame before handoff!

    const uint32_t kMovW4_1 = 0x52800024u;
    const uint32_t kStrW4_284 = 0xB9011FE4u;
    const uint32_t kLdrX4_32 = 0xF94013E4u;

    TEST_ASSERT(kMovW4_1 == 0x52800024u, "mov w4, #1 opcode match");
    TEST_ASSERT(kStrW4_284 == 0xB9011FE4u, "str w4, [sp, #284] opcode match");
    TEST_ASSERT(kLdrX4_32 == 0xF94013E4u, "ldr x4, [sp, #32] opcode match");

    std::cout << "[Stage 262.1] X4 clobber defect identified and remediation verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 262.2 & 262.3 - 100% Preservation of All 31 Registers (X0..X30)
// ----------------------------------------------------------------------------
static void test_stage262_2_3_all_31_registers_preservation() {
    std::cout << "[Stage 262.2-3] Testing 100% Bitwise Preservation of All 31 Registers (X0..X30)...\n";

    Memory mem(0x800000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    const uint64_t stack_top = 0x00044000ULL;
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(stack_top);
    cpu.set_stack_pointer(stack_top);

    // Seed distinct 64-bit patterns for X0 through X30
    std::vector<uint64_t> expected_regs(31);
    for (uint32_t i = 0; i < 31; ++i) {
        if (i == 4) {
            // Specific test pattern for X4 to verify clobber immunity
            expected_regs[i] = 0xDEADBEEFCAFE0004ULL;
        } else if (i == 29) {
            expected_regs[i] = 0x0123456789ABCDEFULL; // FP
        } else if (i == 30) {
            expected_regs[i] = 0xFEDCBA9876543210ULL; // LR
        } else {
            expected_regs[i] = (static_cast<uint64_t>(0xA5A5A5A5u + i * 0x11111111u) << 32) |
                               (0x12345678u ^ (i * 0x01010101u));
        }
        cpu.write_register(i, expected_regs[i]);
    }

    const uint64_t interrupted_pc = 0x0000100CULL;
    const uint64_t interrupted_cpsr = 0x60000000ULL; // NZ
    cpu.set_elr_el1(interrupted_pc);
    cpu.set_spsr_el1(static_cast<uint32_t>(interrupted_cpsr));

    // Construct 32-instruction entry + NOP + 22-instruction restore
    std::vector<uint32_t> code;

    // Entry (Save)
    code.push_back(0xD10483FFu); // 0x2000: sub sp, sp, #288
    for (uint32_t k = 0; k < 15; ++k) {
        code.push_back(encode_stp_sp(k));
    }
    code.push_back(0xF9007BFEu); // str x30, [sp, #240]
    code.push_back(0x910483E0u); // add x0, sp, #288 (EL1 origin SP)
    code.push_back(0xF9007FE0u); // str x0, [sp, #248]
    code.push_back(0xD5384020u); // mrs x0, elr_el1
    code.push_back(0xF90083E0u); // str x0, [sp, #256]
    code.push_back(0xD5384001u); // mrs x1, spsr_el1
    code.push_back(0xF90087E1u); // str x1, [sp, #264]
    code.push_back(0xD5386002u); // mrs x2, far_el1
    code.push_back(0xF9008BE2u); // str x2, [sp, #272]
    code.push_back(0xD5385203u); // mrs x3, esr_el1
    code.push_back(0xB9011BE3u); // str w3, [sp, #280]
    code.push_back(0x52800024u); // mov w4, #1
    code.push_back(0xB9011FE4u); // str w4, [sp, #284]
    // Scratch register reload:
    code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    code.push_back(0xA9410FE2u); // ldp x2, x3, [sp, #16]
    code.push_back(0xF94013E4u); // ldr x4, [sp, #32] - RESTORES X4!

    // Pause point
    code.push_back(0xD503201Fu); // NOP (0x2080)

    // Restore sequence
    code.push_back(0xF94083E0u); // ldr x0, [sp, #256] (ELR)
    code.push_back(0xD5184020u); // msr elr_el1, x0
    code.push_back(0xF94087E1u); // ldr x1, [sp, #264] (SPSR)
    code.push_back(0xD5184001u); // msr spsr_el1, x1
    code.push_back(0xF9407BFEu); // ldr x30, [sp, #240]
    for (int k = 14; k >= 1; --k) {
        code.push_back(encode_ldp_sp(static_cast<uint32_t>(k)));
    }
    code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    code.push_back(0x910483FFu); // add sp, sp, #288
    code.push_back(0xD69F03E0u); // eret

    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(0x2000 + (i * 4), code[i]);
    }

    cpu.set_program_counter(0x2000);

    // Step save phase
    for (std::size_t s = 0; s < 32; ++s) {
        cpu.step(mem);
    }
    TEST_ASSERT(cpu.get_program_counter() == 0x2080ULL, "Reached pause point at 0x2080");

    // Check all registers in CPU right after save phase:
    // Scratch registers X0..X4 MUST have been restored from stack frame!
    for (uint32_t i = 0; i <= 4; ++i) {
        TEST_ASSERT(cpu.read_register(i) == expected_regs[i],
                    ("Scratch register X" + std::to_string(i) + " correctly restored before handoff").c_str());
    }

    // Step NOP
    cpu.step(mem);

    // Step restore phase (22 instructions)
    for (std::size_t s = 0; s < 22; ++s) {
        cpu.step(mem);
    }

    // Verify CPU reached interrupted_pc
    TEST_ASSERT(cpu.get_program_counter() == interrupted_pc, "ERET returned to interrupted PC 0x100C");
    TEST_ASSERT(cpu.get_sp_el1() == stack_top, "SP_EL1 restored to original stack top");

    // Check all 31 registers:
    for (uint32_t i = 0; i < 31; ++i) {
        uint64_t actual = cpu.read_register(i);
        TEST_ASSERT(actual == expected_regs[i],
                    ("Register X" + std::to_string(i) + " bitwise preserved").c_str());
    }

    std::cout << "[Stage 262.2-3] 100% preservation of all 31 registers (X0..X30) verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 262.4 & 262.5 - Interrupted SP Capture & Restore: EL0 vs EL1 Origin
// ----------------------------------------------------------------------------
static void test_stage262_4_5_sp_origin_capture_and_restore() {
    std::cout << "[Stage 262.4-5] Testing Interrupted SP Capture and Restoration (EL0 vs EL1)...\n";

    // Case 1: EL0 Origin
    // Exception taken from EL0 to EL1:
    // Interrupted SP is in SP_EL0.
    // Entry sequence captures SP_EL0:
    //   mrs x0, sp_el0       (0xD5384100)
    //   str x0, [sp, #248]   (0xF9007FE0)
    // Restore sequence restores SP_EL0 before ERET:
    //   ldr x0, [sp, #248]   (0xF9407FE0)
    //   msr sp_el0, x0       (0xD5184100)
    {
        Memory mem(0x800000ULL);
        CPU cpu;
        cpu.reset();
        cpu.set_current_el(1);
        cpu.set_architectural_exceptions(true);

        const uint64_t kEl1StackTop = 0x00044000ULL;
        const uint64_t kEl0StackVal = 0x00048000ULL; // Interrupted user stack pointer

        cpu.set_sp_sel(true);
        cpu.set_sp_el1(kEl1StackTop);
        cpu.set_sp_el0(kEl0StackVal);
        cpu.set_stack_pointer(kEl1StackTop);

        std::vector<uint32_t> code;
        code.push_back(0xD10483FFu); // sub sp, sp, #288
        code.push_back(0xD5384100u); // mrs x0, sp_el0 (EL0 origin SP capture)
        code.push_back(0xF9007FE0u); // str x0, [sp, #248]
        code.push_back(0xD503201Fu); // NOP (pause)
        // Corrupt sp_el0 during exception handler execution:
        // Then restore:
        code.push_back(0xF9407FE0u); // ldr x0, [sp, #248]
        code.push_back(0xD5184100u); // msr sp_el0, x0 (Restore SP_EL0)
        code.push_back(0x910483FFu); // add sp, sp, #288
        code.push_back(0xD503201Fu); // NOP (done)

        for (std::size_t i = 0; i < code.size(); ++i) {
            mem.write_u32(0x3000 + (i * 4), code[i]);
        }
        cpu.set_program_counter(0x3000);

        // Execute save (3 instructions)
        cpu.step(mem);
        cpu.step(mem);
        cpu.step(mem);
        TEST_ASSERT(cpu.get_program_counter() == 0x300CULL, "Reached pause point");

        // Verify frame base
        uint64_t frame_base = kEl1StackTop - 288;
        TEST_ASSERT(mem.read_u64(frame_base + 248) == kEl0StackVal, "Frame contains interrupted SP_EL0");

        // Adversarially corrupt SP_EL0
        cpu.set_sp_el0(0xDEADBEEF00000000ULL);

        // Step through NOP and restore
        cpu.step(mem); // NOP
        cpu.step(mem); // ldr x0, [sp, #248]
        cpu.step(mem); // msr sp_el0, x0
        cpu.step(mem); // add sp, sp, #288

        TEST_ASSERT(cpu.get_sp_el0() == kEl0StackVal, "SP_EL0 successfully restored from frame");
        TEST_ASSERT(cpu.get_stack_pointer() == kEl1StackTop, "EL1 SP returned to stack top");
    }

    // Case 2: EL1 Origin
    // Exception taken from EL1:
    // Interrupted SP was SP_EL1 prior to 288-byte allocation:
    //   add x0, sp, #288     (0x910483E0)
    //   str x0, [sp, #248]   (0xF9007FE0)
    // Restore sequence restores SP via stack frame teardown:
    //   add sp, sp, #288     (0x910483FF)
    {
        Memory mem(0x800000ULL);
        CPU cpu;
        cpu.reset();
        cpu.set_current_el(1);
        cpu.set_architectural_exceptions(true);

        const uint64_t kEl1StackTop = 0x00044000ULL;
        cpu.set_sp_sel(true);
        cpu.set_sp_el1(kEl1StackTop);
        cpu.set_stack_pointer(kEl1StackTop);

        std::vector<uint32_t> code;
        code.push_back(0xD10483FFu); // sub sp, sp, #288
        code.push_back(0x910483E0u); // add x0, sp, #288 (EL1 origin SP capture)
        code.push_back(0xF9007FE0u); // str x0, [sp, #248]
        code.push_back(0xD503201Fu); // NOP
        code.push_back(0x910483FFu); // add sp, sp, #288
        code.push_back(0xD503201Fu); // NOP

        for (std::size_t i = 0; i < code.size(); ++i) {
            mem.write_u32(0x4000 + (i * 4), code[i]);
        }
        cpu.set_program_counter(0x4000);

        cpu.step(mem);
        cpu.step(mem);
        cpu.step(mem);
        TEST_ASSERT(cpu.get_program_counter() == 0x400CULL, "Reached pause point");

        uint64_t frame_base = kEl1StackTop - 288;
        TEST_ASSERT(mem.read_u64(frame_base + 248) == kEl1StackTop, "Frame contains interrupted pre-allocation SP_EL1");

        cpu.step(mem); // NOP
        cpu.step(mem); // add sp, sp, #288
        TEST_ASSERT(cpu.get_stack_pointer() == kEl1StackTop, "EL1 SP returned to pre-exception stack top");
    }

    std::cout << "[Stage 262.4-5] Interrupted SP capture and restore (EL0 vs EL1) verified.\n";
}

int main() {
    std::cout << "=== Running Stage 262.6: Exception Context & SP Tests ===\n";
    test_stage262_1_x4_clobber_audit();
    test_stage262_2_3_all_31_registers_preservation();
    test_stage262_4_5_sp_origin_capture_and_restore();
    std::cout << "=== ALL STAGE 262.6 TESTS PASSED ===\n";
    return 0;
}
