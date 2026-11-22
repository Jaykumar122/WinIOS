// ============================================================================
// Stage 259.5 - 259.8: Complete Context Save Trampoline (with SP at 0x0F8),
// Complete Context Restore Trampoline (with ELR/SPSR reload), Architectural
// Interrupted SP/Metadata Verification, and Frame-Driven Restoration Adversarial Test
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
// Test 1: Stage 259.5: Complete Context Frame Save Trampoline Generation
// ----------------------------------------------------------------------------
static void test_stage259_5_save_trampoline_generation() {
    std::cout << "[Stage 259.5] Testing Complete Context Frame Save Trampoline Generation...\n";

    std::vector<uint32_t> save_seq;

    // 1. sub sp, sp, #288 (0x120) -> 0xD10483FF
    save_seq.push_back(0xD10483FFu);

    // 2. stp x0..x29 in pairs [sp, #0] .. [sp, #224] (15 pairs = 30 registers)
    for (uint32_t k = 0; k < 15; ++k) {
        save_seq.push_back(encode_stp_sp(k));
    }

    // 3. str x30, [sp, #240] -> 0xF9007BFE
    save_seq.push_back(0xF9007BFEu);

    // 4. Save interrupted SP at [sp, #248] (offset 0x0F8 in ArmSavedState64):
    // add x0, sp, #288 -> 0x910483E0 (computes pre-exception SP value into x0)
    // str x0, [sp, #248] -> 0xF9007FE0
    save_seq.push_back(0x910483E0u);
    save_seq.push_back(0xF9007FE0u);

    // 5. Save ELR_EL1 (PC) to [sp, #256]:
    // mrs x0, elr_el1  -> 0xD5384020
    // str x0, [sp, #256] -> 0xF90083E0
    save_seq.push_back(0xD5384020u);
    save_seq.push_back(0xF90083E0u);

    // 6. Save SPSR_EL1 (CPSR) to [sp, #264]:
    // mrs x1, spsr_el1 -> 0xD5384001
    // str x1, [sp, #264] -> 0xF90087E1
    save_seq.push_back(0xD5384001u);
    save_seq.push_back(0xF90087E1u);

    // 7. Save FAR_EL1 to [sp, #272]:
    // mrs x2, far_el1  -> 0xD5386002
    // str x2, [sp, #272] -> 0xF9008BE2
    save_seq.push_back(0xD5386002u);
    save_seq.push_back(0xF9008BE2u);

    // 8. Save ESR_EL1 to [sp, #280]:
    // mrs x3, esr_el1  -> 0xD5385203
    // str x3, [sp, #280] -> 0xF9008FE3
    save_seq.push_back(0xD5385203u);
    save_seq.push_back(0xF9008FE3u);

    // 9. Restore scratch registers X0..X3 clobbered during metadata save:
    // ldp x0, x1, [sp, #0]  -> 0xA94007E0
    // ldp x2, x3, [sp, #16] -> 0xA9410FE2
    save_seq.push_back(0xA94007E0u);
    save_seq.push_back(0xA9410FE2u);

    TEST_ASSERT(save_seq.size() == 29, "Complete save trampoline consists of exactly 29 instructions");
    TEST_ASSERT(save_seq[0] == 0xD10483FFu, "Instruction 0 is sub sp, sp, #288");
    TEST_ASSERT(save_seq[1] == 0xA90007E0u, "Instruction 1 is stp x0, x1, [sp, #0]");
    TEST_ASSERT(save_seq[15] == 0xA90E77FCu, "Instruction 15 is stp x28, x29, [sp, #224]");
    TEST_ASSERT(save_seq[16] == 0xF9007BFEu, "Instruction 16 is str x30, [sp, #240]");
    TEST_ASSERT(save_seq[17] == 0x910483E0u, "Instruction 17 is add x0, sp, #288");
    TEST_ASSERT(save_seq[18] == 0xF9007FE0u, "Instruction 18 is str x0, [sp, #248]");
    TEST_ASSERT(save_seq[19] == 0xD5384020u, "Instruction 19 is mrs x0, elr_el1");
    TEST_ASSERT(save_seq[20] == 0xF90083E0u, "Instruction 20 is str x0, [sp, #256]");

    std::cout << "[Stage 259.5] Complete context save trampoline generated and validated.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 259.6: Complete Context Frame Restore Trampoline Generation
// ----------------------------------------------------------------------------
static void test_stage259_6_restore_trampoline_generation() {
    std::cout << "[Stage 259.6] Testing Complete Context Frame Restore Trampoline Generation...\n";

    std::vector<uint32_t> restore_seq;

    // 1. Reload ELR_EL1 (PC) from [sp, #256] using X0 as scratch:
    // ldr x0, [sp, #256] -> 0xF94083E0
    // msr elr_el1, x0    -> 0xD5184020
    restore_seq.push_back(0xF94083E0u);
    restore_seq.push_back(0xD5184020u);

    // 2. Reload SPSR_EL1 (CPSR) from [sp, #264] using X1 as scratch:
    // ldr x1, [sp, #264] -> 0xF94087E1
    // msr spsr_el1, x1   -> 0xD5184001
    restore_seq.push_back(0xF94087E1u);
    restore_seq.push_back(0xD5184001u);

    // 3. ldr x30, [sp, #240] -> 0xF9407BFE
    restore_seq.push_back(0xF9407BFEu);

    // 4. ldp x28, x29 down to x2, x3 in reverse pair order (14 pairs = 28 registers)
    for (int k = 14; k >= 1; --k) {
        restore_seq.push_back(encode_ldp_sp(static_cast<uint32_t>(k)));
    }

    // 5. Reload X0, X1 from [sp, #0] (restoring guest X0, X1 and overwriting scratch):
    // ldp x0, x1, [sp, #0] -> 0xA94007E0
    restore_seq.push_back(0xA94007E0u);

    // 6. add sp, sp, #288 (0x120) -> 0x910483FF
    restore_seq.push_back(0x910483FFu);

    // 7. eret -> 0xD69F03E0
    restore_seq.push_back(0xD69F03E0u);

    TEST_ASSERT(restore_seq.size() == 22, "Complete restore trampoline consists of exactly 22 instructions");
    TEST_ASSERT(restore_seq[0] == 0xF94083E0u, "Instruction 0 is ldr x0, [sp, #256]");
    TEST_ASSERT(restore_seq[1] == 0xD5184020u, "Instruction 1 is msr elr_el1, x0");
    TEST_ASSERT(restore_seq[2] == 0xF94087E1u, "Instruction 2 is ldr x1, [sp, #264]");
    TEST_ASSERT(restore_seq[3] == 0xD5184001u, "Instruction 3 is msr spsr_el1, x1");
    TEST_ASSERT(restore_seq[4] == 0xF9407BFEu, "Instruction 4 is ldr x30, [sp, #240]");
    TEST_ASSERT(restore_seq[5] == 0xA94E77FCu, "Instruction 5 is ldp x28, x29, [sp, #224]");
    TEST_ASSERT(restore_seq[19] == 0xA94007E0u, "Instruction 19 is ldp x0, x1, [sp, #0]");
    TEST_ASSERT(restore_seq[20] == 0x910483FFu, "Instruction 20 is add sp, sp, #288");
    TEST_ASSERT(restore_seq[21] == 0xD69F03E0u, "Instruction 21 is eret");

    std::cout << "[Stage 259.6] Complete context restore trampoline generated and validated.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 259.7: Architectural Interrupted SP & Metadata Frame Verification
// ----------------------------------------------------------------------------
static void test_stage259_7_sp_and_metadata_frame_verification() {
    std::cout << "[Stage 259.7] Testing Interrupted SP (0x0F8) & Metadata Frame Memory Readback...\n";

    Memory mem(0x800000ULL);

    // Guest code at 0x1000
    mem.write_u32(0x1000, 0xD50342FFu); // msr daifclr, #2
    mem.write_u32(0x1004, 0xD2802014u); // mov x20, #0x100
    mem.write_u32(0x1008, 0xD503207Fu); // wfi
    mem.write_u32(0x100C, 0x91015694u); // add x20, x20, #0x55
    mem.write_u32(0x1010, 0xD280EEF5u); // mov x21, #0x777

    // Build vector entry at 0x0A80 (VBAR + 0x280)
    std::vector<uint32_t> trampoline;

    // Save phase
    trampoline.push_back(0xD10483FFu); // sub sp, sp, #288
    for (uint32_t k = 0; k < 15; ++k) {
        trampoline.push_back(encode_stp_sp(k));
    }
    trampoline.push_back(0xF9007BFEu); // str x30, [sp, #240]
    trampoline.push_back(0x910483E0u); // add x0, sp, #288
    trampoline.push_back(0xF9007FE0u); // str x0, [sp, #248] (SP save)
    trampoline.push_back(0xD5384020u); // mrs x0, elr_el1
    trampoline.push_back(0xF90083E0u); // str x0, [sp, #256] (ELR save)
    trampoline.push_back(0xD5384001u); // mrs x1, spsr_el1
    trampoline.push_back(0xF90087E1u); // str x1, [sp, #264] (SPSR save)
    trampoline.push_back(0xD5386002u); // mrs x2, far_el1
    trampoline.push_back(0xF9008BE2u); // str x2, [sp, #272] (FAR save)
    trampoline.push_back(0xD5385203u); // mrs x3, esr_el1
    trampoline.push_back(0xF9008FE3u); // str x3, [sp, #280] (ESR save)
    trampoline.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    trampoline.push_back(0xA9410FE2u); // ldp x2, x3, [sp, #16]

    // Handler work: simple NOPs/marker
    trampoline.push_back(0xD503201Fu); // nop

    // Restore phase
    trampoline.push_back(0xF94083E0u); // ldr x0, [sp, #256]
    trampoline.push_back(0xD5184020u); // msr elr_el1, x0
    trampoline.push_back(0xF94087E1u); // ldr x1, [sp, #264]
    trampoline.push_back(0xD5184001u); // msr spsr_el1, x1
    trampoline.push_back(0xF9407BFEu); // ldr x30, [sp, #240]
    for (int k = 14; k >= 1; --k) {
        trampoline.push_back(encode_ldp_sp(static_cast<uint32_t>(k)));
    }
    trampoline.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    trampoline.push_back(0x910483FFu); // add sp, sp, #288
    trampoline.push_back(0xD69F03E0u); // eret

    for (std::size_t i = 0; i < trampoline.size(); ++i) {
        mem.write_u32(0x00000A80ULL + (i * 4), trampoline[i]);
    }

    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_vbar_el1(0x00000800ULL);
    cpu.set_sp_el1(0x00044000ULL); // Frame 16 top

    // Seed distinct register values
    for (uint32_t r = 0; r < 29; ++r) {
        cpu.write_register(r, 0x11110000ULL + r);
    }
    cpu.write_register(29, 0x29292929ULL); // FP
    cpu.write_register(30, 0x30303030ULL); // LR
    cpu.write_register(20, 0x100ULL);

    cpu.set_program_counter(0x1000ULL);

    // Step msr daifclr
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1004, "PC is 0x1004");
    // Step mov x20
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1008, "PC is 0x1008");
    // Step wfi -> transitions to waiting_for_interrupt
    cpu.step(mem);
    TEST_ASSERT(cpu.is_waiting_for_interrupt(), "CPU in WFI state");

    // Deliver IRQ 44
    cpu.wake_from_wfi();
    cpu.raise_asynchronous_exception(false, 44, false, 0);
    TEST_ASSERT(cpu.get_program_counter() == 0x0A80, "Vectored to 0x0A80 (CurrentEL_SPx IRQ)");
    TEST_ASSERT(cpu.get_elr_el1() == 0x100C, "ELR_EL1 recorded preferred return 0x100C");

    // Execute save sequence (29 instructions)
    for (int i = 0; i < 29; ++i) {
        cpu.step(mem);
    }

    // Stack pointer is now at 0x43EE0
    const uint64_t frame_sp = cpu.get_sp_el1();
    TEST_ASSERT(frame_sp == 0x00043EE0ULL, "SP is 0x43EE0 after sub sp, sp, #288");

    // Read back frame from memory at 0x43EE0
    // Offset 0x0F8 (248) must contain the interrupted SP (0x00044000)
    uint64_t saved_sp = mem.read_u64(0x00043EE0ULL + 0x0F8);
    TEST_ASSERT(saved_sp == 0x00044000ULL, "Frame offset 0x0F8 contains interrupted SP 0x00044000");

    // Offset 0x100 (256) must contain ELR_EL1 (0x100C)
    uint64_t saved_pc = mem.read_u64(0x00043EE0ULL + 0x100);
    TEST_ASSERT(saved_pc == 0x0000100CULL, "Frame offset 0x100 contains ELR_EL1 0x100C");

    // Offset 0x0E8 (232) must contain FP (X29)
    uint64_t saved_fp = mem.read_u64(0x00043EE0ULL + 0x0E8);
    TEST_ASSERT(saved_fp == 0x29292929ULL, "Frame offset 0x0E8 contains FP 0x29292929");

    // Offset 0x0F0 (240) must contain LR (X30)
    uint64_t saved_lr = mem.read_u64(0x00043EE0ULL + 0x0F0);
    TEST_ASSERT(saved_lr == 0x30303030ULL, "Frame offset 0x0F0 contains LR 0x30303030");

    std::cout << "[Stage 259.7] Interrupted SP (0x0F8 = 0x44000) and metadata verified in memory.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 259.8: Frame-Driven PC/PSTATE Restoration & Adversarial Test
// ----------------------------------------------------------------------------
static void test_stage259_8_frame_driven_pc_pstate_adversarial_test() {
    std::cout << "[Stage 259.8] Testing Frame-Driven PC/PSTATE Restoration (Adversarial live-register corruption)...\n";

    Memory mem(0x800000ULL);

    // Guest code at 0x1000
    mem.write_u32(0x1000, 0xD50342FFu); // msr daifclr, #2
    mem.write_u32(0x1004, 0xD2802014u); // mov x20, #0x100
    mem.write_u32(0x1008, 0xD503207Fu); // wfi
    mem.write_u32(0x100C, 0x91015694u); // add x20, x20, #0x55
    mem.write_u32(0x1010, 0xD280EEF5u); // mov x21, #0x777

    // Build vector entry at 0x0A80
    std::vector<uint32_t> trampoline;

    // Save phase (29 instructions)
    trampoline.push_back(0xD10483FFu); // sub sp, sp, #288
    for (uint32_t k = 0; k < 15; ++k) {
        trampoline.push_back(encode_stp_sp(k));
    }
    trampoline.push_back(0xF9007BFEu); // str x30, [sp, #240]
    trampoline.push_back(0x910483E0u); // add x0, sp, #288
    trampoline.push_back(0xF9007FE0u); // str x0, [sp, #248]
    trampoline.push_back(0xD5384020u); // mrs x0, elr_el1
    trampoline.push_back(0xF90083E0u); // str x0, [sp, #256]
    trampoline.push_back(0xD5384001u); // mrs x1, spsr_el1
    trampoline.push_back(0xF90087E1u); // str x1, [sp, #264]
    trampoline.push_back(0xD5386002u); // mrs x2, far_el1
    trampoline.push_back(0xF9008BE2u); // str x2, [sp, #272]
    trampoline.push_back(0xD5385203u); // mrs x3, esr_el1
    trampoline.push_back(0xF9008FE3u); // str x3, [sp, #280]
    trampoline.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    trampoline.push_back(0xA9410FE2u); // ldp x2, x3, [sp, #16]

    // NOP separator (1 instruction)
    trampoline.push_back(0xD503201Fu);

    // Restore phase (22 instructions)
    trampoline.push_back(0xF94083E0u); // ldr x0, [sp, #256]
    trampoline.push_back(0xD5184020u); // msr elr_el1, x0
    trampoline.push_back(0xF94087E1u); // ldr x1, [sp, #264]
    trampoline.push_back(0xD5184001u); // msr spsr_el1, x1
    trampoline.push_back(0xF9407BFEu); // ldr x30, [sp, #240]
    for (int k = 14; k >= 1; --k) {
        trampoline.push_back(encode_ldp_sp(static_cast<uint32_t>(k)));
    }
    trampoline.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    trampoline.push_back(0x910483FFu); // add sp, sp, #288
    trampoline.push_back(0xD69F03E0u); // eret

    for (std::size_t i = 0; i < trampoline.size(); ++i) {
        mem.write_u32(0x00000A80ULL + (i * 4), trampoline[i]);
    }

    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_vbar_el1(0x00000800ULL);
    cpu.set_sp_el1(0x00044000ULL);

    cpu.write_register(20, 0x100ULL);
    cpu.set_program_counter(0x1000ULL);

    // Run to WFI
    cpu.step(mem); // msr daifclr
    cpu.step(mem); // mov x20
    cpu.step(mem); // wfi

    // Deliver IRQ 44
    cpu.wake_from_wfi();
    cpu.raise_asynchronous_exception(false, 44, false, 0);
    TEST_ASSERT(cpu.get_program_counter() == 0x0A80, "Vectored to 0x0A80");

    // Execute save phase (29 instructions)
    for (int i = 0; i < 29; ++i) {
        cpu.step(mem);
    }

    // Step 1 NOP
    cpu.step(mem);

    // ADVERSARIAL ATTACK: Deliberately corrupt live CPU registers!
    // Corrupt ELR_EL1 to a bogus address
    cpu.set_elr_el1(0xDEADBEEF00000000ULL);
    // Corrupt SPSR_EL1
    cpu.set_spsr_el1(0x00000000u);
    // Corrupt all general-purpose registers
    for (uint32_t r = 0; r <= 30; ++r) {
        cpu.write_register(r, 0xCCCCCCCCCCCCCCCCULL);
    }

    TEST_ASSERT(cpu.get_elr_el1() == 0xDEADBEEF00000000ULL, "ELR_EL1 corrupted to 0xDEADBEEF");

    // Execute restore phase (22 instructions)
    for (int i = 0; i < 22; ++i) {
        cpu.step(mem);
    }

    // Verify: ERET took the PC from the frame restored into ELR_EL1, NOT from the corrupted 0xDEADBEEF!
    TEST_ASSERT(cpu.get_program_counter() == 0x0000100CULL, "PC successfully restored to 0x100C via frame memory");
    TEST_ASSERT(cpu.get_sp_el1() == 0x00044000ULL, "SP restored to 0x44000");

    // Now resume guest continuation:
    cpu.step(mem); // add x20, x20, #0x55
    TEST_ASSERT(cpu.read_register(20) == 0x155ULL, "Continuation instruction updated X20 to 0x155");

    cpu.step(mem); // mov x21, #0x777
    TEST_ASSERT(cpu.read_register(21) == 0x777ULL, "Continuation completed, X21 is 0x777");

    std::cout << "[Stage 259.8] Adversarial live-register corruption defeated by authoritative frame restoration.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 259: Stages 259.5 - 259.8 Context Save & Restore Tests\n";
    std::cout << "====================================================================\n";

    test_stage259_5_save_trampoline_generation();
    test_stage259_6_restore_trampoline_generation();
    test_stage259_7_sp_and_metadata_frame_verification();
    test_stage259_8_frame_driven_pc_pstate_adversarial_test();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 259.5 - 259.8 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
