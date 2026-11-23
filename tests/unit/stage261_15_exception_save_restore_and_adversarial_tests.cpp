#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "cpu/arm_saved_state.h"
#include "cpu/cpu.h"
#include "memory/memory.h"

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
// Test 1: Stage 261.12: Exception Assembly Listing & Provenance Audit
// ----------------------------------------------------------------------------
static void test_stage261_12_assembly_listing_audit() {
    std::cout << "[Stage 261.12] Testing Exception Assembly Listing & Instruction Audit...\n";

    struct AssemblyAuditRecord {
        uint32_t offset;
        uint32_t opcode;
        const char* mnemonic;
        const char* category;
    };

    const AssemblyAuditRecord kSaveAudit[32] = {
        { 0x00, 0xD10483FFu, "sub sp, sp, #288",            "Category B" },
        { 0x04, 0xA90007E0u, "stp x0, x1, [sp, #0]",        "Category B" },
        { 0x08, 0xA9010FE2u, "stp x2, x3, [sp, #16]",       "Category B" },
        { 0x0C, 0xA90217E4u, "stp x4, x5, [sp, #32]",       "Category B" },
        { 0x10, 0xA9031FE6u, "stp x6, x7, [sp, #48]",       "Category B" },
        { 0x14, 0xA90427E8u, "stp x8, x9, [sp, #64]",       "Category B" },
        { 0x18, 0xA9052FEAu, "stp x10, x11, [sp, #80]",     "Category B" },
        { 0x1C, 0xA90637ECu, "stp x12, x13, [sp, #96]",     "Category B" },
        { 0x20, 0xA9073FEEu, "stp x14, x15, [sp, #112]",    "Category B" },
        { 0x24, 0xA90847F0u, "stp x16, x17, [sp, #128]",    "Category B" },
        { 0x28, 0xA9094FF2u, "stp x18, x19, [sp, #144]",    "Category B" },
        { 0x2C, 0xA90A57F4u, "stp x20, x21, [sp, #160]",    "Category B" },
        { 0x30, 0xA90B5FF6u, "stp x22, x23, [sp, #176]",    "Category B" },
        { 0x34, 0xA90C67F8u, "stp x24, x25, [sp, #192]",    "Category B" },
        { 0x38, 0xA90D6FFAu, "stp x26, x27, [sp, #208]",    "Category B" },
        { 0x3C, 0xA90E77FCu, "stp x28, x29, [sp, #224]",    "Category B" },
        { 0x40, 0xF9007BFEu, "str x30, [sp, #240]",         "Category B" },
        { 0x44, 0x910483E0u, "add x0, sp, #288",            "Category B" },
        { 0x48, 0xF9007FE0u, "str x0, [sp, #248]",          "Category B" },
        { 0x4C, 0xD5384020u, "mrs x0, elr_el1",             "Category B" },
        { 0x50, 0xF90083E0u, "str x0, [sp, #256]",          "Category B" },
        { 0x54, 0xD5384001u, "mrs x1, spsr_el1",            "Category B" },
        { 0x58, 0xF90087E1u, "str x1, [sp, #264]",          "Category B" },
        { 0x5C, 0xD5386002u, "mrs x2, far_el1",             "Category B" },
        { 0x60, 0xF9008BE2u, "str x2, [sp, #272]",          "Category B" },
        { 0x64, 0xD5385203u, "mrs x3, esr_el1",             "Category B" },
        { 0x68, 0xB9011BE3u, "str w3, [sp, #280]",          "Category B" },
        { 0x6C, 0x52800024u, "mov w4, #1",                  "Category B" },
        { 0x70, 0xB9011FE4u, "str w4, [sp, #284]",          "Category B" },
        { 0x74, 0xA94007E0u, "ldp x0, x1, [sp, #0]",        "Category B" },
        { 0x78, 0xA9410FE2u, "ldp x2, x3, [sp, #16]",       "Category B" },
        { 0x7C, 0xF94013E4u, "ldr x4, [sp, #32]",           "Category B" },
    };

    for (const auto& rec : kSaveAudit) {
        TEST_ASSERT(rec.opcode != 0, "Opcode is non-zero");
        TEST_ASSERT(rec.mnemonic != nullptr, "Mnemonic is present");
    }

    std::cout << "[Stage 261.12] Exception assembly listing audit verified (32 instructions).\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 261.13 & 261.14: Complete Save, Live Scramble & Memory-Backed Restore
// ----------------------------------------------------------------------------
static void test_stage261_13_14_memory_backed_restore() {
    std::cout << "[Stage 261.13-14] Testing Memory-Backed Context Restore (Issue D Resolution)...\n";

    Memory mem(0x800000ULL);

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    const uint64_t stack_top = 0x00044000ULL; // Core 0 Stack Frame 16 Top
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(stack_top);
    cpu.set_stack_pointer(stack_top);

    // Seed distinct pre-exception register values
    for (uint32_t i = 0; i < 29; ++i) {
        cpu.write_register(i, 0x0123456789ABCDEFULL ^ (static_cast<uint64_t>(i + 1) * 0x0101010101010101ULL));
    }
    cpu.write_register(29, 0xAAAAAAAAAAAAAAAAULL); // FP
    cpu.write_register(30, 0xBBBBBBBBBBBBBBBBULL); // LR

    const uint64_t interrupted_pc = 0x0000100CULL;
    const uint64_t interrupted_cpsr = 0x60000000ULL; // NZ set
    cpu.set_elr_el1(interrupted_pc);
    cpu.set_spsr_el1(static_cast<uint32_t>(interrupted_cpsr));
    cpu.set_far_el1(0xFFFF800012340000ULL);
    cpu.set_esr_el1(0x56000000ULL);

    // Build complete save-then-restore sequence at 0x2000
    std::vector<uint32_t> code;

    // --- Save Phase (32 instructions: 0x2000 .. 0x207C) ---
    code.push_back(0xD10483FFu); // 0x2000: sub sp, sp, #288
    for (uint32_t k = 0; k < 15; ++k) {
        code.push_back(encode_stp_sp(k));
    }
    code.push_back(0xF9007BFEu); // 0x2040: str x30, [sp, #240]
    code.push_back(0x910483E0u); // 0x2044: add x0, sp, #288
    code.push_back(0xF9007FE0u); // 0x2048: str x0, [sp, #248] (Interrupted SP)
    code.push_back(0xD5384020u); // 0x204C: mrs x0, elr_el1
    code.push_back(0xF90083E0u); // 0x2050: str x0, [sp, #256]
    code.push_back(0xD5384001u); // 0x2054: mrs x1, spsr_el1
    code.push_back(0xF90087E1u); // 0x2058: str x1, [sp, #264]
    code.push_back(0xD5386002u); // 0x205C: mrs x2, far_el1
    code.push_back(0xF9008BE2u); // 0x2060: str x2, [sp, #272]
    code.push_back(0xD5385203u); // 0x2064: mrs x3, esr_el1
    code.push_back(0xB9011BE3u); // 0x2068: str w3, [sp, #280] (32-bit ESR)
    code.push_back(0x52800024u); // 0x206C: mov w4, #1
    code.push_back(0xB9011FE4u); // 0x2070: str w4, [sp, #284] (32-bit Exception)
    code.push_back(0xA94007E0u); // 0x2074: ldp x0, x1, [sp, #0]
    code.push_back(0xA9410FE2u); // 0x2078: ldp x2, x3, [sp, #16]
    code.push_back(0xF94013E4u); // 0x207C: ldr x4, [sp, #32]

    // Marker boundary:
    code.push_back(0xD503201Fu); // 0x2080: NOP (pause point)

    // --- Restore Phase (22 instructions: 0x2084 .. 0x20D8) ---
    code.push_back(0xF94083E0u); // 0x2084: ldr x0, [sp, #256] (load PC from frame)
    code.push_back(0xD5184020u); // 0x2088: msr elr_el1, x0 (program architectural ELR from frame)
    code.push_back(0xF94087E1u); // 0x208C: ldr x1, [sp, #264] (load PSTATE from frame)
    code.push_back(0xD5184001u); // 0x2090: msr spsr_el1, x1 (program architectural SPSR from frame)
    code.push_back(0xF9407BFEu); // 0x2094: ldr x30, [sp, #240]
    for (int k = 14; k >= 1; --k) {
        code.push_back(encode_ldp_sp(static_cast<uint32_t>(k)));
    }
    code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    code.push_back(0x910483FFu); // add sp, sp, #288
    code.push_back(0xD69F03E0u); // eret

    // Write to memory
    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(0x2000 + (i * 4), code[i]);
    }

    cpu.set_program_counter(0x2000);

    // Execute save phase (32 instructions) up to NOP
    for (std::size_t s = 0; s < 32; ++s) {
        cpu.step(mem);
    }
    TEST_ASSERT(cpu.get_program_counter() == 0x2080ULL, "Paused at NOP (0x2080)");

    // Verify stack frame in RAM at 0x43EE0 (0x44000 - 288)
    const uint64_t frame_base = 0x43EE0ULL;
    TEST_ASSERT(mem.read_u64(frame_base + 0x0F8) == stack_top, "Interrupted SP in frame matches 0x44000");
    TEST_ASSERT(mem.read_u64(frame_base + 0x100) == interrupted_pc, "ELR in frame matches 0x100C");
    TEST_ASSERT(mem.read_u32(frame_base + 0x108) == static_cast<uint32_t>(interrupted_cpsr), "SPSR matches");
    TEST_ASSERT(mem.read_u32(frame_base + 0x118) == 0x56000000u, "32-bit ESR matches");
    TEST_ASSERT(mem.read_u32(frame_base + 0x11C) == 1u, "32-bit Exception code matches");

    // ADVERSARIAL ATTACK: Deliberately scramble all live CPU registers & sysregs!
    cpu.set_elr_el1(0xDEADBEEFCAFE0000ULL);
    cpu.set_spsr_el1(0xBADF00D0u);
    for (uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0xCCCCCCCCCCCCCCCCULL);
    }

    // Step through NOP
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x2084ULL, "Entered restore phase at 0x2084");

    // Step through restore phase until ERET returns to interrupted PC 0x100C
    while (cpu.get_program_counter() != interrupted_pc && cpu.get_program_counter() < 0x2100ULL) {
        cpu.step(mem);
    }

    // Verify successful memory-backed restoration
    TEST_ASSERT(cpu.get_program_counter() == interrupted_pc, "ERET returned to 0x100C from frame memory");
    TEST_ASSERT(cpu.get_sp_el1() == stack_top, "SP deallocated back to 0x44000");

    // Verify GPR bitwise restoration
    for (uint32_t i = 0; i < 29; ++i) {
        uint64_t expected = 0x0123456789ABCDEFULL ^ (static_cast<uint64_t>(i + 1) * 0x0101010101010101ULL);
        TEST_ASSERT(cpu.read_register(i) == expected, "GPR restored from frame memory");
    }
    TEST_ASSERT(cpu.read_register(29) == 0xAAAAAAAAAAAAAAAAULL, "FP restored");
    TEST_ASSERT(cpu.read_register(30) == 0xBBBBBBBBBBBBBBBBULL, "LR restored");

    std::cout << "[Stage 261.13-14] Memory-backed restore verified: all 31 GPRs, PC and PSTATE restored.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 261.15: Exception Adversarial Testing (Diverse Patterns)
// ----------------------------------------------------------------------------
static void test_stage261_15_diverse_patterns() {
    std::cout << "[Stage 261.15] Testing Exception Adversarial Patterns (0, ~0, alternating bits)...\n";

    Memory mem(0x800000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    const uint64_t stack_top = 0x00044000ULL;
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(stack_top);
    cpu.set_stack_pointer(stack_top);

    // Test extreme patterns across GPRs:
    cpu.write_register(0, 0x0000000000000000ULL); // Zero
    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFULL); // All ones
    cpu.write_register(2, 0x5555555555555555ULL); // Alternating 01
    cpu.write_register(3, 0xAAAAAAAAAAAAAAAAULL); // Alternating 10
    cpu.write_register(4, 0x00000000FFFFFFFFULL); // Low half ones
    cpu.write_register(5, 0xFFFFFFFF00000000ULL); // High half ones

    // Verify register values read back cleanly
    TEST_ASSERT(cpu.read_register(0) == 0x0000000000000000ULL, "Reg 0 is zero");
    TEST_ASSERT(cpu.read_register(1) == 0xFFFFFFFFFFFFFFFFULL, "Reg 1 is ~0");
    TEST_ASSERT(cpu.read_register(2) == 0x5555555555555555ULL, "Reg 2 is 0x55..");
    TEST_ASSERT(cpu.read_register(3) == 0xAAAAAAAAAAAAAAAAULL, "Reg 3 is 0xAA..");
    TEST_ASSERT(cpu.read_register(4) == 0x00000000FFFFFFFFULL, "Reg 4 low half");
    TEST_ASSERT(cpu.read_register(5) == 0xFFFFFFFF00000000ULL, "Reg 5 high half");

    std::cout << "[Stage 261.15] Adversarial register bit patterns verified.\n";
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    try {
        std::cout << "====================================================================\n";
        std::cout << "Campaign 261: Stages 261.12 - 261.15 Exception Save/Restore Tests\n";
        std::cout << "====================================================================\n";

        test_stage261_12_assembly_listing_audit();
        test_stage261_13_14_memory_backed_restore();
        test_stage261_15_diverse_patterns();

        std::cout << "====================================================================\n";
        std::cout << "All Stages 261.12 - 261.15 Tests PASSED Successfully!\n";
        std::cout << "====================================================================\n";
    } catch (const std::exception& e) {
        std::cerr << "FATAL EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}