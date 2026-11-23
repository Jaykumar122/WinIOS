// =============================================================================
// WinIOS Campaign 265 - Stages 265.1 through 265.4 Test Suite
//
// Stage 265.1: Campaign 264 Implementation & Discrepancy Inventory
// Stage 265.2: Independent AArch64 Instruction Decoder
// Stage 265.3: Reproduce and Correct STR/LDR Encoding Defect
// Stage 265.4: Complete Generated Instruction Stream Audit
// =============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cassert>

#include "cpu/arm_saved_state.h"
#include "cpu/aarch64_instruction_decoder.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                      << " - " << (msg) << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::cpu;

// -----------------------------------------------------------------------------
// Stage 265.1: Discrepancy Inventory Verification
// -----------------------------------------------------------------------------
static bool test_stage265_1_discrepancy_inventory() {
    std::cout << "[Stage 265.1] Auditing Campaign 264 discrepancies...\n";

    // Reported words from Campaign 264:
    const std::uint32_t reported_str_w1 = 0xB9042FE1u;
    const std::uint32_t reported_ldr_w1 = 0xB9442FE1u;

    // Independent decode of reported words:
    auto d_str = AArch64InstructionDecoder::decode(reported_str_w1);
    auto d_ldr = AArch64InstructionDecoder::decode(reported_ldr_w1);

    // Verify they encoded offset 1068, NOT 264!
    TEST_ASSERT(d_str.is_valid, "Reported STR word must be valid AArch64");
    TEST_ASSERT(d_str.mnemonic == "str", "Reported STR mnemonic is str");
    TEST_ASSERT(d_str.register_width == 32, "Reported STR width is 32-bit");
    TEST_ASSERT(d_str.rn == 31, "Reported STR base is SP (31)");
    TEST_ASSERT(d_str.rt == 1, "Reported STR target is W1 (1)");
    TEST_ASSERT(d_str.immediate_offset == 1068, "PROVEN: Reported STR encoded offset 1068, NOT 264!");

    TEST_ASSERT(d_ldr.is_valid, "Reported LDR word must be valid AArch64");
    TEST_ASSERT(d_ldr.mnemonic == "ldr", "Reported LDR mnemonic is ldr");
    TEST_ASSERT(d_ldr.register_width == 32, "Reported LDR width is 32-bit");
    TEST_ASSERT(d_ldr.rn == 31, "Reported LDR base is SP (31)");
    TEST_ASSERT(d_ldr.rt == 1, "Reported LDR target is W1 (1)");
    TEST_ASSERT(d_ldr.immediate_offset == 1068, "PROVEN: Reported LDR encoded offset 1068, NOT 264!");

    std::cout << "  - Reported 0xB9042FE1 decodes to: " << d_str.disassembly << "\n";
    std::cout << "  - Reported 0xB9442FE1 decodes to: " << d_ldr.disassembly << "\n";
    std::cout << "  [PASS] Stage 265.1 Discrepancy successfully reproduced & proven.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 265.2: Independent AArch64 Instruction Decoder Test
// -----------------------------------------------------------------------------
static bool test_stage265_2_independent_decoder() {
    std::cout << "[Stage 265.2] Testing independent AArch64 instruction decoder...\n";

    // 1. ERET
    auto d_eret = AArch64InstructionDecoder::decode(0xD69F03E0u);
    TEST_ASSERT(d_eret.is_valid && d_eret.mnemonic == "eret", "ERET decode");

    // 2. SUB SP, SP, #288
    // imm12 = 288 = 0x120. (288 << 10) = 0x48000. 0xD10003FF | 0x48000 = 0xD10483FF
    auto d_sub = AArch64InstructionDecoder::decode(0xD10483FFu);
    TEST_ASSERT(d_sub.is_valid && d_sub.mnemonic == "sub", "SUB decode mnemonic");
    TEST_ASSERT(d_sub.rn == 31 && d_sub.rt == 31, "SUB SP, SP");
    TEST_ASSERT(d_sub.immediate_offset == 288, "SUB SP, SP, #288 immediate");

    // 3. ADD SP, SP, #288
    auto d_add = AArch64InstructionDecoder::decode(0x910483FFu);
    TEST_ASSERT(d_add.is_valid && d_add.mnemonic == "add", "ADD decode mnemonic");
    TEST_ASSERT(d_add.immediate_offset == 288, "ADD SP, SP, #288 immediate");

    // 4. STP X0, X1, [SP, #0]
    auto d_stp = AArch64InstructionDecoder::decode(0xA90007E0u);
    TEST_ASSERT(d_stp.is_valid && d_stp.mnemonic == "stp", "STP decode mnemonic");
    TEST_ASSERT(d_stp.rt == 0 && d_stp.rt2 == 1 && d_stp.rn == 31, "STP X0, X1, [sp]");
    TEST_ASSERT(d_stp.immediate_offset == 0, "STP offset 0");

    // 5. LDP X0, X1, [SP, #0]
    auto d_ldp = AArch64InstructionDecoder::decode(0xA94007E0u);
    TEST_ASSERT(d_ldp.is_valid && d_ldp.mnemonic == "ldp", "LDP decode mnemonic");
    TEST_ASSERT(d_ldp.rt == 0 && d_ldp.rt2 == 1 && d_ldp.rn == 31, "LDP X0, X1, [sp]");

    // 6. 64-bit STR X30, [SP, #240] (0xF9007BFE)
    auto d_str_x30 = AArch64InstructionDecoder::decode(0xF9007BFEu);
    TEST_ASSERT(d_str_x30.is_valid && d_str_x30.mnemonic == "str", "STR X30 decode");
    TEST_ASSERT(d_str_x30.register_width == 64, "STR X30 width 64");
    TEST_ASSERT(d_str_x30.rt == 30 && d_str_x30.rn == 31, "STR X30, [sp]");
    TEST_ASSERT(d_str_x30.immediate_offset == 240, "STR X30 offset 240");

    // 7. System Register MRS / MSR
    auto d_mrs_sp0 = AArch64InstructionDecoder::decode(0xD5384100u);
    TEST_ASSERT(d_mrs_sp0.is_valid && d_mrs_sp0.mnemonic == "mrs", "MRS SP_EL0 mnemonic");
    TEST_ASSERT(d_mrs_sp0.sysreg_name == "sp_el0", "MRS sysreg sp_el0");

    auto d_msr_elr = AArch64InstructionDecoder::decode(0xD5184020u);
    TEST_ASSERT(d_msr_elr.is_valid && d_msr_elr.mnemonic == "msr", "MSR ELR_EL1 mnemonic");
    TEST_ASSERT(d_msr_elr.sysreg_name == "elr_el1", "MSR sysreg elr_el1");

    std::cout << "  [PASS] Stage 265.2 Independent AArch64 decoder fully operational.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 265.3: Reproduce and Correct STR/LDR Encoding Defect
// -----------------------------------------------------------------------------
static bool test_stage265_3_correct_str_ldr_encoding() {
    std::cout << "[Stage 265.3] Testing and verifying corrected STR/LDR encodings...\n";

    // 1. Check constexpr encoder functions
    constexpr std::uint32_t c_str_w1 = encode_str_w(1, 31, 264);
    constexpr std::uint32_t c_ldr_w1 = encode_ldr_w(1, 31, 264);
    constexpr std::uint32_t c_str_w2 = encode_str_w(2, 31, 268);
    constexpr std::uint32_t c_str_w4 = encode_str_w(4, 31, 280);
    constexpr std::uint32_t c_str_wzr = encode_str_w(31, 31, 284);

    // Exact expected hex values
    TEST_ASSERT(c_str_w1 == 0xB9010BE1u, "encode_str_w(1, 31, 264) == 0xB9010BE1");
    TEST_ASSERT(c_ldr_w1 == 0xB9410BE1u, "encode_ldr_w(1, 31, 264) == 0xB9410BE1");
    TEST_ASSERT(c_str_w2 == 0xB9010FE2u, "encode_str_w(2, 31, 268) == 0xB9010FE2");
    TEST_ASSERT(c_str_w4 == 0xB9011BE4u, "encode_str_w(4, 31, 280) == 0xB9011BE4");
    TEST_ASSERT(c_str_wzr == 0xB9011FFFu, "encode_str_w(31, 31, 284) == 0xB9011FFF");

    // 2. Decode each with independent decoder
    auto d1 = AArch64InstructionDecoder::decode(c_str_w1);
    TEST_ASSERT(d1.is_valid && d1.mnemonic == "str" && d1.register_width == 32, "STR W1 valid 32-bit");
    TEST_ASSERT(d1.rt == 1 && d1.rn == 31 && d1.immediate_offset == 264, "STR W1, [SP, #264] exact");

    auto d2 = AArch64InstructionDecoder::decode(c_ldr_w1);
    TEST_ASSERT(d2.is_valid && d2.mnemonic == "ldr" && d2.register_width == 32, "LDR W1 valid 32-bit");
    TEST_ASSERT(d2.rt == 1 && d2.rn == 31 && d2.immediate_offset == 264, "LDR W1, [SP, #264] exact");

    auto d3 = AArch64InstructionDecoder::decode(c_str_w2);
    TEST_ASSERT(d3.is_valid && d3.mnemonic == "str" && d3.immediate_offset == 268, "STR W2, [SP, #268] exact");

    auto d4 = AArch64InstructionDecoder::decode(c_str_w4);
    TEST_ASSERT(d4.is_valid && d4.mnemonic == "str" && d4.immediate_offset == 280, "STR W4, [SP, #280] exact");

    auto d5 = AArch64InstructionDecoder::decode(c_str_wzr);
    TEST_ASSERT(d5.is_valid && d5.mnemonic == "str" && d5.immediate_offset == 284 && d5.rt == 31, "STR WZR, [SP, #284] exact");

    // 3. Verify adjacent field boundaries:
    // CPSR is at [264..267] (4 bytes). Next is FSR at [268..271] (4 bytes).
    // Storing W1 at 264 writes exactly bytes 264, 265, 266, 267.
    // Storing W2 at 268 writes exactly bytes 268, 269, 270, 271.
    TEST_ASSERT(d1.immediate_offset + 4 == d3.immediate_offset, "Zero gap and zero overlap between CPSR (264) and FSR (268)");

    std::cout << "  - Corrected STR W1: " << d1.disassembly << "\n";
    std::cout << "  - Corrected LDR W1: " << d2.disassembly << "\n";
    std::cout << "  - Corrected STR W2: " << d3.disassembly << "\n";
    std::cout << "  - Corrected STR W4: " << d4.disassembly << "\n";
    std::cout << "  - Corrected STR WZR: " << d5.disassembly << "\n";
    std::cout << "  [PASS] Stage 265.3 Corrected STR/LDR encodings verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 265.4: Complete Generated Instruction Stream Audit
// -----------------------------------------------------------------------------
static bool test_stage265_4_instruction_stream_audit() {
    std::cout << "[Stage 265.4] Auditing complete generated instruction stream...\n";

    const ExecutionProfile profiles[] = { ExecutionProfile::ARM64_Standard, ExecutionProfile::ARM64e_PAC };
    const bool origins[] = { true, false };

    for (auto profile : profiles) {
        for (bool is_el0 : origins) {
            auto entry_code = encode_exception_entry_sequence(profile, is_el0);
            auto restore_code = encode_exception_restore_sequence(profile, is_el0);

            TEST_ASSERT(!entry_code.empty(), "Entry code not empty");
            TEST_ASSERT(!restore_code.empty(), "Restore code not empty");

            // Audit every instruction in entry sequence
            for (std::size_t idx = 0; idx < entry_code.size(); ++idx) {
                std::uint32_t insn = entry_code[idx];
                auto decoded = AArch64InstructionDecoder::decode(insn);
                TEST_ASSERT(decoded.is_valid, "Every entry instruction must decode to valid AArch64");

                // Check critical invariants
                if (idx == 0) {
                    TEST_ASSERT(decoded.mnemonic == "sub" && decoded.rn == 31 && decoded.rt == 31,
                                "Instruction 0 must be sub sp, sp, #frame_size");
                    std::int64_t expected_size = (profile == ExecutionProfile::ARM64_Standard) ? 288 : 304;
                    TEST_ASSERT(decoded.immediate_offset == expected_size, "Frame allocation size match");
                }
            }

            // Audit every instruction in restore sequence
            for (std::size_t idx = 0; idx < restore_code.size(); ++idx) {
                std::uint32_t insn = restore_code[idx];
                auto decoded = AArch64InstructionDecoder::decode(insn);
                TEST_ASSERT(decoded.is_valid, "Every restore instruction must decode to valid AArch64");

                // Last instruction must be ERET
                if (idx + 1 == restore_code.size()) {
                    TEST_ASSERT(decoded.mnemonic == "eret", "Last restore instruction must be ERET");
                }
            }
        }
    }

    std::cout << "  [PASS] Stage 265.4 Complete instruction stream audited: 100% valid AArch64.\n";
    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Campaign 265 - Stages 265.1..265.4 Instruction Encoding Tests\n";
    std::cout << "=================================================================\n";

    if (!test_stage265_1_discrepancy_inventory()) return 1;
    if (!test_stage265_2_independent_decoder()) return 1;
    if (!test_stage265_3_correct_str_ldr_encoding()) return 1;
    if (!test_stage265_4_instruction_stream_audit()) return 1;

    std::cout << "=================================================================\n";
    std::cout << "All Stages 265.1..265.4 Tests Passed Successfully!\n";
    std::cout << "=================================================================\n";
    return 0;
}
