// ============================================================================
// Stage 258.5 - 258.8: Full 31-Register Save & Restore Trampoline Generation,
// Distinct Register-File Values Harness, Direct 288-Byte Frame Memory Readback
// & Full 31-Register Post-ERET Integrity Assertion
// ============================================================================

#include "cpu/arm_saved_state.h"
#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "memory/memory.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <cstring>

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
// Test 1: Stage 258.5: Full 31-Register Save Trampoline Generation
// ----------------------------------------------------------------------------
static void test_stage258_5_save_trampoline_generation() {
    std::cout << "[Stage 258.5] Testing Full 31-Register Save Trampoline Generation...\n";

    std::vector<uint32_t> save_seq;

    // 1. sub sp, sp, #288 (0x120) -> 0xD10483FF
    save_seq.push_back(0xD10483FFu);

    // 2. stp x0..x29 in pairs [sp, #0] .. [sp, #224] (15 pairs = 30 registers)
    for (uint32_t k = 0; k < 15; ++k) {
        save_seq.push_back(encode_stp_sp(k));
    }

    // 3. str x30, [sp, #240] -> 0xF9007BFE
    save_seq.push_back(0xF9007BFEu);

    // 4. Save ELR_EL1 (PC) to [sp, #256]:
    // mrs x0, elr_el1  -> 0xD5384020
    // str x0, [sp, #256] -> 0xF90083E0
    save_seq.push_back(0xD5384020u);
    save_seq.push_back(0xF90083E0u);

    // 5. Save SPSR_EL1 (CPSR) to [sp, #264]:
    // mrs x1, spsr_el1 -> 0xD5384001
    // str x1, [sp, #264] -> 0xF90087E1
    save_seq.push_back(0xD5384001u);
    save_seq.push_back(0xF90087E1u);

    // 6. Save FAR_EL1 to [sp, #272]:
    // mrs x2, far_el1  -> 0xD5386002
    // str x2, [sp, #272] -> 0xF9008BE2
    save_seq.push_back(0xD5386002u);
    save_seq.push_back(0xF9008BE2u);

    // 7. Save ESR_EL1 to [sp, #280]:
    // mrs x3, esr_el1  -> 0xD5385203
    // str x3, [sp, #280] -> 0xF9008FE3
    save_seq.push_back(0xD5385203u);
    save_seq.push_back(0xF9008FE3u);

    // 8. Restore scratch registers X0..X3 clobbered during metadata save:
    // ldp x0, x1, [sp, #0]  -> 0xA94007E0
    // ldp x2, x3, [sp, #16] -> 0xA9410FE2
    save_seq.push_back(0xA94007E0u);
    save_seq.push_back(0xA9410FE2u);

    TEST_ASSERT(save_seq.size() == 27, "Save trampoline consists of exactly 27 instructions");
    TEST_ASSERT(save_seq[0] == 0xD10483FFu, "Instruction 0 is sub sp, sp, #288");
    TEST_ASSERT(save_seq[1] == 0xA90007E0u, "Instruction 1 is stp x0, x1, [sp, #0]");
    TEST_ASSERT(save_seq[15] == 0xA90E77FCu, "Instruction 15 is stp x28, x29, [sp, #224]");
    TEST_ASSERT(save_seq[16] == 0xF9007BFEu, "Instruction 16 is str x30, [sp, #240]");

    std::cout << "[Stage 258.5] Full 31-register save trampoline generated and validated.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 258.6: Full 31-Register Restore Trampoline Generation
// ----------------------------------------------------------------------------
static void test_stage258_6_restore_trampoline_generation() {
    std::cout << "[Stage 258.6] Testing Full 31-Register Restore Trampoline Generation...\n";

    std::vector<uint32_t> restore_seq;

    // 1. ldr x30, [sp, #240] -> 0xF9407BFE
    restore_seq.push_back(0xF9407BFEu);

    // 2. ldp x28, x29 down to x0, x1 in reverse pair order
    for (int k = 14; k >= 0; --k) {
        restore_seq.push_back(encode_ldp_sp(static_cast<uint32_t>(k)));
    }

    // 3. add sp, sp, #288 (0x120) -> 0x910483FF
    restore_seq.push_back(0x910483FFu);

    // 4. eret -> 0xD69F03E0
    restore_seq.push_back(0xD69F03E0u);

    TEST_ASSERT(restore_seq.size() == 18, "Restore trampoline consists of exactly 18 instructions");
    TEST_ASSERT(restore_seq[0] == 0xF9407BFEu, "Instruction 0 is ldr x30, [sp, #240]");
    TEST_ASSERT(restore_seq[1] == 0xA94E77FCu, "Instruction 1 is ldp x28, x29, [sp, #224]");
    TEST_ASSERT(restore_seq[15] == 0xA94007E0u, "Instruction 15 is ldp x0, x1, [sp, #0]");
    TEST_ASSERT(restore_seq[16] == 0x910483FFu, "Instruction 16 is add sp, sp, #288");
    TEST_ASSERT(restore_seq[17] == 0xD69F03E0u, "Instruction 17 is eret");

    std::cout << "[Stage 258.6] Full 31-register restore trampoline generated and validated.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 258.7 & 258.8: Full 31-Register Preserved Values, Memory Readback & ERET
// ----------------------------------------------------------------------------
static void test_stage258_7_8_full_31_register_preservation_and_readback() {
    std::cout << "[Stage 258.7-8] Testing Full 31-Register Preservation, Frame Readback & Post-ERET Integrity...\n";

    Memory mem(0x800000ULL);

    // 1. Setup interrupted guest workload at 0x1000
    // 0x1000: msr daifclr, #2       (unmask IRQ)
    // 0x1004: mov x20, #0x100       (guest payload)
    // 0x1008: wfi                   (wait for interrupt)
    // 0x100C: add x20, x20, #0x55   (resumed continuation)
    // 0x1010: mov x21, #0x777       (completion marker)
    mem.write_u32(0x1000, 0xD50342FFu);
    mem.write_u32(0x1004, 0xD2802014u);
    mem.write_u32(0x1008, 0xD503207Fu); // wfi
    mem.write_u32(0x100C, 0x91015694u); // add x20, x20, #0x55
    mem.write_u32(0x1010, 0xD280EEF5u); // mov x21, #0x777

    // 2. Assemble complete full-context trampoline at 0x00000A80 (VBAR_EL1 + 0x280)
    std::vector<uint32_t> full_trampoline;

    // --- Save Phase (28 instructions) ---
    full_trampoline.push_back(0xD10483FFu); // sub sp, sp, #288
    for (uint32_t k = 0; k < 15; ++k) {
        full_trampoline.push_back(encode_stp_sp(k));
    }
    full_trampoline.push_back(0xF9007BFEu); // str x30, [sp, #240]
    full_trampoline.push_back(0xD5384020u); // mrs x0, elr_el1
    full_trampoline.push_back(0xF90083E0u); // str x0, [sp, #256]
    full_trampoline.push_back(0xD5384001u); // mrs x1, spsr_el1
    full_trampoline.push_back(0xF90087E1u); // str x1, [sp, #264]
    full_trampoline.push_back(0xD5386002u); // mrs x2, far_el1
    full_trampoline.push_back(0xF9008BE2u); // str x2, [sp, #272]
    full_trampoline.push_back(0xD5385203u); // mrs x3, esr_el1
    full_trampoline.push_back(0xF9008FE3u); // str x3, [sp, #280]
    full_trampoline.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    full_trampoline.push_back(0xA9410FE2u); // ldp x2, x3, [sp, #16]

    // --- Handler Internal Operation ---
    // Perform simulated interrupt handler work: modify X2, X3, X4 temporarily
    full_trampoline.push_back(0x91000442u); // add x2, x2, #1
    full_trampoline.push_back(0x91000463u); // add x3, x3, #1
    full_trampoline.push_back(0x91000484u); // add x4, x4, #1

    // --- Restore Phase (18 instructions) ---
    full_trampoline.push_back(0xF9407BFEu); // ldr x30, [sp, #240]
    for (int k = 14; k >= 0; --k) {
        full_trampoline.push_back(encode_ldp_sp(static_cast<uint32_t>(k)));
    }
    full_trampoline.push_back(0x910483FFu); // add sp, sp, #288
    full_trampoline.push_back(0xD69F03E0u); // eret

    // Write trampoline into guest memory at 0x00000A80
    for (std::size_t i = 0; i < full_trampoline.size(); ++i) {
        mem.write_u32(0x00000A80ULL + (i * 4), full_trampoline[i]);
    }

    // 3. Initialize CPU
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x00000800ULL);
    cpu.set_sp_el1(0x00044000ULL); // Top of int_stack (Frame 16)
    cpu.set_program_counter(0x1000ULL);

    // Initialize ALL 31 general-purpose registers (X0..X30) with distinct values
    for (std::size_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0xCAFE0000ULL + i);
    }

    // 4. Step setup instructions up to WFI
    cpu.step(mem); // 0x1000: msr daifclr
    cpu.step(mem); // 0x1004: mov x20, #0x100
    cpu.step(mem); // 0x1008: wfi

    // Assert asynchronous IRQ 44
    cpu.wake_from_wfi();
    cpu.raise_asynchronous_exception(false, 44, false, 0);

    TEST_ASSERT(cpu.get_program_counter() == 0x00000A80ULL, "Exception vectored to 0x0A80");
    TEST_ASSERT(cpu.get_elr_el1() == 0x100CULL, "ELR_EL1 captured return address 0x100C");

    // 5. Step through Save Phase (27 instructions)
    for (int i = 0; i < 27; ++i) {
        cpu.step(mem);
    }

    // SP must now be at 0x43EE0
    TEST_ASSERT(cpu.get_sp_el1() == 0x00043EE0ULL, "SP is 0x43EE0 after allocating 288-byte frame");

    // Direct memory readback verifying the 288-byte ArmSavedState64 frame in guest RAM
    const uint64_t frame_addr = 0x00043EE0ULL;
    for (std::size_t i = 0; i < 29; ++i) {
        uint64_t val = mem.read_u64(frame_addr + (i * 8));
        if (i == 20) {
            TEST_ASSERT(val == 0x100ULL, "Frame X20 matches interrupted value 0x100");
        } else {
            TEST_ASSERT(val == (0xCAFE0000ULL + i), "Frame Xi matches initial test pattern");
        }
    }

    // Verify FP (X29) at offset 0x0E8 (232)
    uint64_t frame_fp = mem.read_u64(frame_addr + 0x0E8);
    TEST_ASSERT(frame_fp == 0xCAFE001DULL, "Frame FP matches X29 (0xCAFE001D)");

    // Verify LR (X30) at offset 0x0F0 (240)
    uint64_t frame_lr = mem.read_u64(frame_addr + 0x0F0);
    TEST_ASSERT(frame_lr == 0xCAFE001EULL, "Frame LR matches X30 (0xCAFE001E)");

    // Verify PC (ELR_EL1) at offset 0x100 (256)
    uint64_t frame_pc = mem.read_u64(frame_addr + 0x100);
    TEST_ASSERT(frame_pc == 0x100CULL, "Frame PC matches interrupted ELR_EL1 (0x100C)");

    // 6. Step through handler work (3 instructions)
    for (int i = 0; i < 3; ++i) {
        cpu.step(mem);
    }
    // Verify handler modified X2, X3, X4 temporarily
    TEST_ASSERT(cpu.read_register(2) != 0xCAFE0002ULL, "X2 clobbered during handler processing");
    TEST_ASSERT(cpu.read_register(3) != 0xCAFE0003ULL, "X3 clobbered during handler processing");
    TEST_ASSERT(cpu.read_register(4) != 0xCAFE0004ULL, "X4 clobbered during handler processing");

    // 7. Step through Restore Phase (18 instructions) including ERET
    for (int i = 0; i < 18; ++i) {
        cpu.step(mem);
    }

    // 8. Post-ERET Integrity Assertions
    TEST_ASSERT(cpu.get_program_counter() == 0x100CULL, "ERET successfully returned to 0x100C");
    TEST_ASSERT(cpu.get_sp_el1() == 0x00044000ULL, "SP restored to exact initial 0x44000");

    // Assert ALL 31 registers (X0..X30) are intact with zero corruption!
    for (std::size_t i = 0; i < 31; ++i) {
        if (i == 20) {
            TEST_ASSERT(cpu.read_register(20) == 0x100ULL, "X20 has preserved value 0x100");
        } else {
            TEST_ASSERT(cpu.read_register(i) == (0xCAFE0000ULL + i), "Register Xi preserved across exception boundary");
        }
    }

    // Step continuation instructions
    cpu.step(mem); // 0x100C: add x20, x20, #0x55
    TEST_ASSERT(cpu.read_register(20) == 0x155ULL, "Continuation executed: 0x100 + 0x55 = 0x155");

    cpu.step(mem); // 0x1010: mov x21, #0x777
    TEST_ASSERT(cpu.read_register(21) == 0x777ULL, "Final completion marker 0x777 verified");

    std::cout << "[Stage 258.7-8] All 31 registers preserved, frame readback verified, post-ERET execution successful.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 258: Stages 258.5 - 258.8 31-Register Save & Restore Tests\n";
    std::cout << "====================================================================\n";

    test_stage258_5_save_trampoline_generation();
    test_stage258_6_restore_trampoline_generation();
    test_stage258_7_8_full_31_register_preservation_and_readback();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 258.5 - 258.8 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
