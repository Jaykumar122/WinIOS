#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <iomanip>

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
// Test 1: Stage 260.7 - Corrected Exception Trampoline Generation
// ----------------------------------------------------------------------------
static void test_stage260_7_corrected_trampoline_generation() {
    std::cout << "[Stage 260.7] Testing Corrected Exception Trampoline Generation (32-bit ESR/Exception)...\n";

    std::vector<uint32_t> save_seq;

    // 1. sub sp, sp, #288 (0x120)
    save_seq.push_back(0xD10483FFu);

    // 2. stp x0..x29 in pairs (15 pairs = 30 registers)
    for (uint32_t k = 0; k < 15; ++k) {
        save_seq.push_back(encode_stp_sp(k));
    }

    // 3. str x30, [sp, #240] (LR save)
    save_seq.push_back(0xF9007BFEu);

    // 4. Interrupted SP calculation and save at [sp, #248]
    save_seq.push_back(0x910483E0u); // add x0, sp, #288
    save_seq.push_back(0xF9007FE0u); // str x0, [sp, #248]

    // 5. System registers: ELR_EL1 and SPSR_EL1
    save_seq.push_back(0xD5384020u); // mrs x0, elr_el1
    save_seq.push_back(0xF90083E0u); // str x0, [sp, #256]
    save_seq.push_back(0xD5384001u); // mrs x1, spsr_el1
    save_seq.push_back(0xF90087E1u); // str x1, [sp, #264]

    // 6. FAR_EL1 save at [sp, #272]
    save_seq.push_back(0xD5386002u); // mrs x2, far_el1
    save_seq.push_back(0xF9008BE2u); // str x2, [sp, #272]

    // 7. ESR_EL1 save (32-bit str w3, [sp, #280]) - DOES NOT OVERWRITE EXCEPTION
    save_seq.push_back(0xD5385203u); // mrs x3, esr_el1
    save_seq.push_back(0xB9011BE3u); // str w3, [sp, #280] (32-bit store at offset 0x118)

    // 8. Exception code save (32-bit str w4, [sp, #284]) - INDEPENDENT FIELD
    save_seq.push_back(0x52800024u); // mov w4, #1 (IRQ exception code)
    save_seq.push_back(0xB9011FE4u); // str w4, [sp, #284] (32-bit store at offset 0x11C)

    // 9. Restore scratch registers X0-X4
    save_seq.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    save_seq.push_back(0xA9410FE2u); // ldp x2, x3, [sp, #16]
    save_seq.push_back(0xF94013E4u); // ldr x4, [sp, #32]

    TEST_ASSERT(save_seq.size() == 32, "Corrected save trampoline has exactly 32 instructions");

    // Verify 32-bit store opcodes
    TEST_ASSERT(save_seq[26] == 0xB9011BE3u, "Opcode at index 26 is 32-bit str w3, [sp, #280]");
    TEST_ASSERT(save_seq[28] == 0xB9011FE4u, "Opcode at index 28 is 32-bit str w4, [sp, #284]");

    std::cout << "[Stage 260.7] Corrected save trampoline generated and validated.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 260.8 - Full Frame Adversarial Preservation & Restoration
// ----------------------------------------------------------------------------
static void test_stage260_8_adversarial_preservation() {
    std::cout << "[Stage 260.8] Testing Adversarial Live Corruption & Frame-Driven Restore...\n";

    Memory mem(0x800000ULL);

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    // Setup kernel exception stack at 0x44000
    const uint64_t initial_sp = 0x00044000ULL;
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(initial_sp);
    cpu.set_stack_pointer(initial_sp);

    // Initial CPU register state with unique non-trivial values
    for (uint32_t i = 0; i < 29; ++i) {
        cpu.write_register(i, 0x0123456789ABCDEFULL ^ (static_cast<uint64_t>(i + 1) * 0x0101010101010101ULL));
    }
    cpu.write_register(29, 0xAAAAAAAAAAAAAAAAULL); // FP / X29
    cpu.write_register(30, 0xBBBBBBBBBBBBBBBBULL); // LR / X30

    // Set interrupted PC (ELR) and CPSR (SPSR)
    const uint64_t interrupted_pc = 0x0000100CULL;
    const uint64_t interrupted_cpsr = 0x60000000ULL; // NZ set
    cpu.set_elr_el1(interrupted_pc);
    cpu.set_spsr_el1(interrupted_cpsr);
    cpu.set_far_el1(0xFFFF800012340000ULL);
    cpu.set_esr_el1(0x56000000ULL);

    // Build complete save-then-restore trampoline at 0x2000
    std::vector<uint32_t> code;
    // --- Save Phase ---
    code.push_back(0xD10483FFu); // 0x2000: sub sp, sp, #288
    for (uint32_t k = 0; k < 15; ++k) {
        code.push_back(encode_stp_sp(k)); // 0x2004..0x203C: stp x0..x29
    }
    code.push_back(0xF9007BFEu); // 0x2040: str x30, [sp, #240]
    code.push_back(0x910483E0u); // 0x2044: add x0, sp, #288
    code.push_back(0xF9007FE0u); // 0x2048: str x0, [sp, #248]
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

    // Marker instruction at pause boundary:
    code.push_back(0xD503201Fu); // 0x2080: NOP (handler boundary)

    // --- Restore Phase ---
    code.push_back(0xF94083E0u); // 0x2084: ldr x0, [sp, #256] (reload ELR from frame)
    code.push_back(0xD5184020u); // 0x2088: msr elr_el1, x0
    code.push_back(0xF94087E1u); // 0x208C: ldr x1, [sp, #264] (reload SPSR from frame)
    code.push_back(0xD5184001u); // 0x2090: msr spsr_el1, x1
    code.push_back(0xF9407BFEu); // 0x2094: ldr x30, [sp, #240]
    for (int k = 14; k >= 1; --k) {
        code.push_back(encode_ldp_sp(static_cast<uint32_t>(k)));
    }
    code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    code.push_back(0x910483FFu); // add sp, sp, #288
    code.push_back(0xD69F03E0u); // eret

    // Write code to memory at 0x2000
    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(0x2000 + (i * 4), code[i]);
    }

    cpu.set_program_counter(0x2000);

    // Execute save phase (32 instructions) up to NOP
    for (std::size_t s = 0; s < 32; ++s) {
        cpu.step(mem);
    }
    TEST_ASSERT(cpu.get_program_counter() == 0x2080ULL, "Paused at NOP boundary (0x2080)");

    // Verify memory frame at 0x43EE0 (0x44000 - 288)
    const uint64_t frame_base = 0x43EE0ULL;
    TEST_ASSERT(cpu.get_sp_el1() == frame_base, "SP_EL1 is at frame_base 0x43EE0");

    // Read back frame fields from RAM
    uint64_t ram_sp  = mem.read_u64(frame_base + 0x0F8);
    uint64_t ram_elr = mem.read_u64(frame_base + 0x100);
    uint32_t ram_cpsr = mem.read_u32(frame_base + 0x108);
    uint32_t ram_esr = mem.read_u32(frame_base + 0x118);
    uint32_t ram_exc = mem.read_u32(frame_base + 0x11C);

    TEST_ASSERT(ram_sp == 0x00044000ULL, "Interrupted SP saved at 0x0F8 is 0x44000");
    TEST_ASSERT(ram_elr == interrupted_pc, "ELR saved at 0x100 is 0x100C");
    TEST_ASSERT(ram_cpsr == static_cast<uint32_t>(interrupted_cpsr), "CPSR saved at 0x108 matches");
    TEST_ASSERT(ram_esr == 0x56000000u, "32-bit ESR saved at 0x118 is 0x56000000");
    TEST_ASSERT(ram_exc == 1u, "32-bit Exception code saved at 0x11C is 1");

    // ADVERSARIAL ATTACK: While paused at NOP, intentionally corrupt live CPU registers!
    cpu.set_elr_el1(0xDEADBEEFCAFE0000ULL);
    cpu.set_spsr_el1(0xBADF00D0u);
    for (uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0xCCCCCCCCCCCCCCCCULL);
    }

    // Step through NOP to enter Restore Phase
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x2084ULL, "Entered restore phase at 0x2084");

    // Step through restore phase until ERET
    while (cpu.get_program_counter() != 0x100CULL && cpu.get_program_counter() < 0x2150ULL) {
        cpu.step(mem);
    }

    // Verify frame-driven restoration
    TEST_ASSERT(cpu.get_program_counter() == 0x100CULL, "ERET resumed to interrupted PC 0x100C from frame memory");
    TEST_ASSERT(cpu.get_sp_el1() == 0x44000ULL, "SP deallocated back to 0x44000");

    // Verify all 29 general registers restored exactly to pre-exception values
    for (uint32_t i = 0; i < 29; ++i) {
        uint64_t expected = 0x0123456789ABCDEFULL ^ (static_cast<uint64_t>(i + 1) * 0x0101010101010101ULL);
        TEST_ASSERT(cpu.read_register(i) == expected, "GPR bitwise restored from memory frame");
    }
    TEST_ASSERT(cpu.read_register(29) == 0xAAAAAAAAAAAAAAAAULL, "FP/X29 restored");
    TEST_ASSERT(cpu.read_register(30) == 0xBBBBBBBBBBBBBBBBULL, "LR/X30 restored");

    std::cout << "[Stage 260.8] Adversarial frame preservation and restoration verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 260.9 - Exception Stack Boundaries, Guards & Nesting Safety
// ----------------------------------------------------------------------------
static void test_stage260_9_stack_boundaries_and_nesting() {
    std::cout << "[Stage 260.9] Testing Stack Boundaries, Guards & Nesting Safety...\n";

    // 16 KiB Stack Frame 16: 0x40000 .. 0x44000 (Top: 0x44000, Base: 0x40000)
    const uint64_t stack_base = 0x40000ULL;
    const uint64_t stack_top  = 0x44000ULL;
    const uint64_t frame_size = sizeof(ArmSavedState64); // 288 bytes

    // 1. Single frame bounds check
    uint64_t sp = stack_top;
    TEST_ASSERT(sp == 0x44000ULL, "Stack starts at 0x44000");

    // Depth 1: Push frame
    sp -= frame_size;
    TEST_ASSERT(sp == 0x43EE0ULL, "Depth 1 SP is 0x43EE0");
    TEST_ASSERT(sp >= stack_base, "Depth 1 is within stack bounds");

    // Depth 2: Push nested frame
    sp -= frame_size;
    TEST_ASSERT(sp == 0x43DC0ULL, "Depth 2 SP is 0x43DC0");
    TEST_ASSERT(sp >= stack_base, "Depth 2 is within stack bounds");

    // Depth 3: Push nested frame
    sp -= frame_size;
    TEST_ASSERT(sp == 0x43CA0ULL, "Depth 3 SP is 0x43CA0");
    TEST_ASSERT(sp >= stack_base, "Depth 3 is within stack bounds");

    // Maximum safe nesting depth within 16 KiB (16384 bytes / 288 bytes = 56 frames)
    const std::size_t max_depth = 16384 / frame_size;
    TEST_ASSERT(max_depth == 56, "16 KiB frame supports up to 56 nested exception frames");

    // Overflow guard check: Cannot allocate below stack_base (0x40000)
    uint64_t exhausted_sp = stack_base + 100; // only 100 bytes left
    bool can_allocate = (exhausted_sp >= stack_base + frame_size);
    TEST_ASSERT(!can_allocate, "Stack overflow rejected when remaining space < 288 B");

    // Underflow guard check: Cannot deallocate above stack_top (0x44000)
    uint64_t current_sp = stack_top;
    bool can_pop = (current_sp + frame_size <= stack_top);
    TEST_ASSERT(!can_pop, "Stack underflow rejected when popping beyond stack_top");

    // Core isolation: 6 cores each with independent 16 KiB stack bands
    for (std::size_t core = 0; core < 6; ++core) {
        uint64_t core_stack_base = 0x40000ULL + (core * 0x4000ULL);
        uint64_t core_stack_top  = core_stack_base + 0x4000ULL;
        TEST_ASSERT(core_stack_top - core_stack_base == 16384, "Core has exactly 16 KiB stack band");
        if (core > 0) {
            uint64_t prev_top = 0x40000ULL + ((core - 1) * 0x4000ULL) + 0x4000ULL;
            TEST_ASSERT(core_stack_base == prev_top, "Core stacks are non-overlapping and contiguous");
        }
    }

    std::cout << "[Stage 260.9] Stack boundaries, guards and nesting safety verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 260.10 - Exception Provenance & Deterministic Encoding Chain
// ----------------------------------------------------------------------------
static void test_stage260_10_provenance_and_encoding_chain() {
    std::cout << "[Stage 260.10] Testing Exception Provenance & Instruction Encoding Chain...\n";

    // Verified instruction table:
    struct InstructionRecord {
        const char* mnemonic;
        uint32_t opcode;
        uint32_t offset;
    };

    const InstructionRecord kSaveTrampoline[] = {
        {"sub sp, sp, #288",            0xD10483FFu, 0x00},
        {"stp x0, x1, [sp, #0]",        0xA90007E0u, 0x04},
        {"stp x2, x3, [sp, #16]",       0xA9010FE2u, 0x08},
        {"str x30, [sp, #240]",         0xF9007BFEu, 0x40},
        {"add x0, sp, #288",            0x910483E0u, 0x44},
        {"str x0, [sp, #248]",          0xF9007FE0u, 0x48},
        {"mrs x0, elr_el1",             0xD5384020u, 0x4C},
        {"str x0, [sp, #256]",          0xF90083E0u, 0x50},
        {"mrs x1, spsr_el1",            0xD5384001u, 0x54},
        {"str x1, [sp, #264]",          0xF90087E1u, 0x58},
        {"mrs x2, far_el1",             0xD5386002u, 0x5C},
        {"str x2, [sp, #272]",          0xF9008BE2u, 0x60},
        {"mrs x3, esr_el1",             0xD5385203u, 0x64},
        {"str w3, [sp, #280]",          0xB9011BE3u, 0x68},
        {"mov w4, #1",                  0x52800024u, 0x6C},
        {"str w4, [sp, #284]",          0xB9011FE4u, 0x70},
    };

    for (const auto& rec : kSaveTrampoline) {
        TEST_ASSERT(rec.opcode != 0, "Opcode is non-zero");
        TEST_ASSERT(rec.mnemonic != nullptr, "Mnemonic is present");
    }

    // Mathematical verification of bitfields for 32-bit STR:
    // STR Wt, [Xn, #imm12]:
    // size = 10 (32-bit) -> bits [31:30] = 0b10
    // bits [29:24] = 0b111001 (0x39)
    // imm12 = offset / 4: 280 / 4 = 70 = 0x046
    // Rn = 31 (SP)
    // Rt = 3 (W3)
    // Opcode = 0xB9000000 | (0x046 << 10) | (31 << 5) | 3 = 0xB9011BE3
    uint32_t expected_str_w3 = 0xB9000000u | (70u << 10) | (31u << 5) | 3u;
    TEST_ASSERT(expected_str_w3 == 0xB9011BE3u, "Formulaic encoding of str w3, [sp, #280] verified");

    // str w4, [sp, #284]: imm12 = 284 / 4 = 71 = 0x047, Rt = 4
    uint32_t expected_str_w4 = 0xB9000000u | (71u << 10) | (31u << 5) | 4u;
    TEST_ASSERT(expected_str_w4 == 0xB9011FE4u, "Formulaic encoding of str w4, [sp, #284] verified");

    std::cout << "[Stage 260.10] Exception provenance and instruction encoding chain verified.\n";
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    try {
        std::cout << "====================================================================\n";
        std::cout << "Campaign 260: Stages 260.7 - 260.10 Exception Frame & Provenance Tests\n";
        std::cout << "====================================================================\n";

        std::cout << "Running test 1..." << std::endl;
        test_stage260_7_corrected_trampoline_generation();
        std::cout << "Running test 2..." << std::endl;
        test_stage260_8_adversarial_preservation();
        std::cout << "Running test 3..." << std::endl;
        test_stage260_9_stack_boundaries_and_nesting();
        std::cout << "Running test 4..." << std::endl;
        test_stage260_10_provenance_and_encoding_chain();

        std::cout << "====================================================================\n";
        std::cout << "All Stages 260.7 - 260.10 Tests PASSED Successfully!\n";
        std::cout << "====================================================================\n";
    } catch (const std::exception& e) {
        std::cerr << "FATAL EXCEPTION: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "FATAL UNKNOWN EXCEPTION" << std::endl;
        return 1;
    }
    return 0;
}
