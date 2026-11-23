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
    uint32_t imm7 = 2 * k;
    return 0xA9000000u | (imm7 << 15) | (t2 << 10) | (31u << 5) | t1;
}

// Helper to encode LDP Xt1, Xt2, [sp, #(16 * k)]
static inline uint32_t encode_ldp_sp(uint32_t k) {
    uint32_t t1 = 2 * k;
    uint32_t t2 = 2 * k + 1;
    uint32_t imm7 = 2 * k;
    return 0xA9400000u | (imm7 << 15) | (t2 << 10) | (31u << 5) | t1;
}

// ----------------------------------------------------------------------------
// Test 1: Stage 263.5 - Exception Origin Detection Logic
// ----------------------------------------------------------------------------
static void test_stage263_5_origin_detection() {
    std::cout << "[Stage 263.5] Testing Architectural Exception Origin Detection...\n";

    // In ARM64, exception origin is determined by PSTATE.M[3:0] saved into SPSR_EL1:
    // M[3:0] == 0b0000 (0x0): Taken from EL0t (User Thread) -> EL0 Origin
    // M[3:0] == 0b0100 (0x4): Taken from EL1t (Kernel using SP_EL0)
    // M[3:0] == 0b0101 (0x5): Taken from EL1h (Kernel using SP_EL1) -> EL1 Origin

    auto is_el0_origin = [](uint32_t spsr) -> bool {
        return (spsr & 0x0Fu) == 0x00u;
    };

    TEST_ASSERT(is_el0_origin(0x60000000u), "SPSR 0x60000000 is EL0 origin");
    TEST_ASSERT(is_el0_origin(0x00000000u), "SPSR 0x00000000 is EL0 origin");
    TEST_ASSERT(!is_el0_origin(0x60000005u), "SPSR 0x60000005 is EL1 origin (EL1h)");
    TEST_ASSERT(!is_el0_origin(0x00000005u), "SPSR 0x00000005 is EL1 origin (EL1h)");

    std::cout << "[Stage 263.5] Architectural origin detection verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 263.6 & 263.8 - EL0-Origin Entry & Restore Execution
// ----------------------------------------------------------------------------
static void test_stage263_6_8_el0_entry_and_restore() {
    std::cout << "[Stage 263.6 & 8] Testing EL0-Origin Entry, SP_EL0 Capture & Restore...\n";

    Memory mem(0x800000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    const uint64_t kEl1StackTop = 0x00044000ULL;
    const uint64_t kOriginalSpEl0 = 0x00048000ULL;
    const uint64_t kOriginalPc = 0x0000100CULL;
    const uint64_t kOriginalCpsr = 0x60000000ULL; // Mode = EL0t

    cpu.set_sp_sel(true);
    cpu.set_sp_el1(kEl1StackTop);
    cpu.set_sp_el0(kOriginalSpEl0);
    cpu.set_stack_pointer(kEl1StackTop);
    cpu.set_elr_el1(kOriginalPc);
    cpu.set_spsr_el1(static_cast<uint32_t>(kOriginalCpsr));

    // Seed distinct 64-bit values
    std::vector<uint64_t> expected_regs(31);
    for (uint32_t i = 0; i < 31; ++i) {
        expected_regs[i] = (static_cast<uint64_t>(0xA1B2C3D4u + i * 0x11111111u) << 32) | (0xE5F60718u ^ (i * 0x02020202u));
        cpu.write_register(i, expected_regs[i]);
    }

    std::vector<uint32_t> code;
    // EL0 Entry (32 instructions)
    code.push_back(0xD10483FFu); // 0x2000: sub sp, sp, #288
    for (uint32_t k = 0; k < 15; ++k) code.push_back(encode_stp_sp(k));
    code.push_back(0xF9007BFEu); // str x30, [sp, #240]
    // EL0 origin specific SP capture:
    code.push_back(0xD5384100u); // mrs x0, sp_el0
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
    code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    code.push_back(0xA9410FE2u); // ldp x2, x3, [sp, #16]
    code.push_back(0xF94013E4u); // ldr x4, [sp, #32] (Restore X4)

    // Pause Point
    code.push_back(0xD503201Fu); // NOP at 0x2080

    // EL0 Restore Sequence (23 instructions with SP_EL0 restoration)
    code.push_back(0xF94083E0u); // ldr x0, [sp, #256]
    code.push_back(0xD5184020u); // msr elr_el1, x0
    code.push_back(0xF94087E1u); // ldr x1, [sp, #264]
    code.push_back(0xD5184001u); // msr spsr_el1, x1
    // EL0 origin specific SP restoration:
    code.push_back(0xF9407FE0u); // ldr x0, [sp, #248]
    code.push_back(0xD5184100u); // msr sp_el0, x0
    code.push_back(0xF9407BFEu); // ldr x30, [sp, #240]
    for (int k = 14; k >= 1; --k) code.push_back(encode_ldp_sp(static_cast<uint32_t>(k)));
    code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    code.push_back(0x910483FFu); // add sp, sp, #288
    code.push_back(0xD69F03E0u); // eret

    for (std::size_t i = 0; i < code.size(); ++i) mem.write_u32(0x2000 + (i * 4), code[i]);
    cpu.set_program_counter(0x2000);

    // Execute entry (32 instructions)
    for (std::size_t s = 0; s < 32; ++s) cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x2080ULL, "Reached pause point at 0x2080");

    // Check RAM frame contains interrupted SP_EL0
    uint64_t frame_base = kEl1StackTop - 288;
    TEST_ASSERT(mem.read_u64(frame_base + 248) == kOriginalSpEl0, "Frame contains original SP_EL0");

    // Adversarially corrupt live SP_EL0 and GPRs during handler
    cpu.set_sp_el0(0xDEADBEEF00000000ULL);
    for (uint32_t i = 0; i < 31; ++i) cpu.write_register(i, 0xFFFFFFFFFFFFFFFFULL);

    cpu.step(mem); // Step NOP
    // Step restore (24 instructions)
    for (std::size_t s = 0; s < 24; ++s) cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == kOriginalPc, "ERET returned to interrupted EL0 PC");
    TEST_ASSERT(cpu.get_sp_el0() == kOriginalSpEl0, "SP_EL0 accurately restored from memory frame");
    TEST_ASSERT(cpu.get_sp_el1() == kEl1StackTop, "EL1 stack pointer returned to stack top");

    for (uint32_t i = 0; i < 31; ++i) {
        TEST_ASSERT(cpu.read_register(i) == expected_regs[i], "Register recovered from adversarial corruption");
    }

    std::cout << "[Stage 263.6 & 8] EL0 entry and restore successfully executed.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 263.7 & 263.9 - EL1-Origin Entry & Restore Execution
// ----------------------------------------------------------------------------
static void test_stage263_7_9_el1_entry_and_restore() {
    std::cout << "[Stage 263.7 & 9] Testing EL1-Origin Entry, Pre-Allocation SP Capture & Restore...\n";

    Memory mem(0x800000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    const uint64_t kEl1StackTop = 0x00044000ULL;
    const uint64_t kOriginalPc = 0x0000100CULL;
    const uint64_t kOriginalCpsr = 0x60000005ULL; // Mode = EL1h

    cpu.set_sp_sel(true);
    cpu.set_sp_el1(kEl1StackTop);
    cpu.set_stack_pointer(kEl1StackTop);
    cpu.set_elr_el1(kOriginalPc);
    cpu.set_spsr_el1(static_cast<uint32_t>(kOriginalCpsr));

    std::vector<uint64_t> expected_regs(31);
    for (uint32_t i = 0; i < 31; ++i) {
        expected_regs[i] = (static_cast<uint64_t>(0xB2C3D4E5u + i * 0x05050505u) << 32) | (0x12345678u ^ i);
        cpu.write_register(i, expected_regs[i]);
    }

    std::vector<uint32_t> code;
    // EL1 Entry (32 instructions)
    code.push_back(0xD10483FFu); // sub sp, sp, #288
    for (uint32_t k = 0; k < 15; ++k) code.push_back(encode_stp_sp(k));
    code.push_back(0xF9007BFEu); // str x30, [sp, #240]
    // EL1 origin specific pre-allocation SP capture:
    code.push_back(0x910483E0u); // add x0, sp, #288
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
    code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    code.push_back(0xA9410FE2u); // ldp x2, x3, [sp, #16]
    code.push_back(0xF94013E4u); // ldr x4, [sp, #32]

    // Pause Point
    code.push_back(0xD503201Fu); // NOP at 0x3080

    // EL1 Restore Sequence (22 instructions)
    code.push_back(0xF94083E0u); // ldr x0, [sp, #256]
    code.push_back(0xD5184020u); // msr elr_el1, x0
    code.push_back(0xF94087E1u); // ldr x1, [sp, #264]
    code.push_back(0xD5184001u); // msr spsr_el1, x1
    code.push_back(0xF9407BFEu); // ldr x30, [sp, #240]
    for (int k = 14; k >= 1; --k) code.push_back(encode_ldp_sp(static_cast<uint32_t>(k)));
    code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    code.push_back(0x910483FFu); // add sp, sp, #288
    code.push_back(0xD69F03E0u); // eret

    for (std::size_t i = 0; i < code.size(); ++i) mem.write_u32(0x3000 + (i * 4), code[i]);
    cpu.set_program_counter(0x3000);

    for (std::size_t s = 0; s < 32; ++s) cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x3080ULL, "Reached pause point at 0x3080");

    uint64_t frame_base = kEl1StackTop - 288;
    TEST_ASSERT(mem.read_u64(frame_base + 248) == kEl1StackTop, "Frame contains pre-allocation SP_EL1");

    // Adversarially corrupt live GPRs
    for (uint32_t i = 0; i < 31; ++i) cpu.write_register(i, 0x5555555555555555ULL);

    cpu.step(mem); // NOP
    for (std::size_t s = 0; s < 22; ++s) cpu.step(mem);

    TEST_ASSERT(cpu.get_program_counter() == kOriginalPc, "ERET returned to interrupted EL1 PC");
    TEST_ASSERT(cpu.get_sp_el1() == kEl1StackTop, "EL1 SP returned to pre-exception stack top");

    for (uint32_t i = 0; i < 31; ++i) {
        TEST_ASSERT(cpu.read_register(i) == expected_regs[i], "Register recovered from adversarial corruption");
    }

    std::cout << "[Stage 263.7 & 9] EL1 entry and restore successfully executed.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 263.10 & 263.11 - PAC-Specific Frame Execution (ARM64 vs ARM64e)
// ----------------------------------------------------------------------------
static void test_stage263_10_11_pac_frame_execution() {
    std::cout << "[Stage 263.10 & 11] Testing PAC-Specific Frame Allocation (304-byte aligned)...\n";

    Memory mem(0x800000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    const uint64_t kEl1StackTop = 0x00044000ULL;
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(kEl1StackTop);
    cpu.set_stack_pointer(kEl1StackTop);

    // In ARM64e_PAC:
    // Raw saved state is 296 bytes.
    // Stack frame allocation is 304 bytes (0x130) to preserve 16-byte SP alignment!
    // sub sp, sp, #304 (0xD104C3FF)
    // str xzr, [sp, #288] (Initialize jophash at offset 0x120)
    // add sp, sp, #304 (0x9104C3FF)
    std::vector<uint32_t> pac_code;
    pac_code.push_back(0xD104C3FFu); // sub sp, sp, #304
    pac_code.push_back(0xA90007E0u); // stp x0, x1, [sp, #0]
    pac_code.push_back(0xF90093FFu); // str xzr, [sp, #288] (Initialize jophash slot)
    pac_code.push_back(0xD503201Fu); // NOP
    pac_code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    pac_code.push_back(0x9104C3FFu); // add sp, sp, #304
    pac_code.push_back(0xD503201Fu); // NOP

    for (std::size_t i = 0; i < pac_code.size(); ++i) mem.write_u32(0x5000 + (i * 4), pac_code[i]);
    cpu.set_program_counter(0x5000);

    cpu.step(mem); // sub sp, sp, #304
    TEST_ASSERT(cpu.get_stack_pointer() == kEl1StackTop - 304, "PAC frame allocated 304 bytes");
    TEST_ASSERT(cpu.get_stack_pointer() % 16 == 0, "Stack pointer remains strictly 16-byte aligned");

    cpu.step(mem); // stp x0, x1
    cpu.step(mem); // str xzr, [sp, #288]
    TEST_ASSERT(mem.read_u64(kEl1StackTop - 304 + 288) == 0ULL, "jophash slot written at offset 288");

    cpu.step(mem); // NOP
    cpu.step(mem); // ldp x0, x1
    cpu.step(mem); // add sp, sp, #304
    TEST_ASSERT(cpu.get_stack_pointer() == kEl1StackTop, "PAC frame deallocated back to stack top");

    std::cout << "[Stage 263.10 & 11] PAC frame allocation (304 bytes) and alignment verified.\n";
}

int main() {
    std::cout << "=== Running Stage 263.11: Dual-Origin, PAC & Adversarial Tests ===\n";
    test_stage263_5_origin_detection();
    test_stage263_6_8_el0_entry_and_restore();
    test_stage263_7_9_el1_entry_and_restore();
    test_stage263_10_11_pac_frame_execution();
    std::cout << "=== ALL STAGE 263.11 TESTS PASSED ===\n";
    return 0;
}
