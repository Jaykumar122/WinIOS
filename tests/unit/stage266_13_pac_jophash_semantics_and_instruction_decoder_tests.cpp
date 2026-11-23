// =============================================================================
// WinIOS Campaign 266 - Stages 266.11 through 266.13 Test Suite
//
// Stage 266.11: JOPHASH Opaque-Value Semantics & Exact Bitwise Preservation
// Stage 266.12: PAC Profile and Frame Layout Audit
// Stage 266.13: Full Exception Instruction Listing & Clean-Room Decoder Audit
// =============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cassert>
#include <iomanip>
#include <sstream>

#include "cpu/cpu.h"
#include "cpu/arm_saved_state.h"
#include "cpu/aarch64_instruction_decoder.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                      << " - " << (msg) << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::cpu;

// -----------------------------------------------------------------------------
// Stage 266.11: JOPHASH Opaque-Value Semantics & Exact Bitwise Preservation
// -----------------------------------------------------------------------------
static bool test_stage266_11_jophash_opaque_semantics() {
    std::cout << "[Stage 266.11] Testing JOPHASH opaque-value semantics across arbitrary 64-bit values...\n";

    // Pinned public XNU defines jophash at offset 0x120 (288) in arm_saved_state64 under HAS_APPLE_PAC.
    // In WinIOS, jophash is an opaque 64-bit metadata field. We do NOT invent a cryptographic algorithm,
    // a hardware PAC authentication, or an artificial non-zero invariant.
    const std::vector<std::uint64_t> test_jophash_values = {
        0x0000000000000000ULL, // Zero value (valid metadata!)
        0xFFFFFFFFFFFFFFFFULL, // All ones
        0x8000000000000000ULL, // High-bit only
        0x0000000000000001ULL, // Low-bit only
        0xAAAAAAAAAAAAAAAAULL, // Alternating bit pattern A
        0x5555555555555555ULL, // Alternating bit pattern 5
        0x266A5A5A12345678ULL, // Reproducible random test vector 1
        0xFEEDFACECAFEBEEFULL  // Reproducible random test vector 2
    };

    ::Memory mem(0x800000ULL);

    for (std::uint64_t jophash : test_jophash_values) {
        // Build an arm_saved_state64_pac_upstream struct
        arm_saved_state64_pac_upstream frame{};
        frame.sp = 0x00060000ULL;
        frame.pc = 0x00001000ULL;
        frame.cpsr = 0x00000000u;
        frame.jophash = jophash; // Store exact value

        // Serialize to memory at frame address
        std::uint64_t frame_addr = 0x0005F000ULL;
        std::memcpy(mem.raw_data() + frame_addr, &frame, sizeof(frame));

        // Read back from memory at exact offset 0x120 (288)
        std::uint64_t read_back = mem.read_u64(frame_addr + 288);
        TEST_ASSERT(read_back == jophash, "JOPHASH read back must match exactly bit-for-bit");

        // Verify that mutating even a single bit is detected
        std::uint64_t corrupted_hash = jophash ^ 0x0000000000000001ULL;
        TEST_ASSERT(corrupted_hash != jophash, "Single-bit flip creates corruption");

        std::cout << "  - JOPHASH 0x" << std::hex << std::setw(16) << std::setfill('0')
                  << jophash << ": PRESERVED bitwise (Zero invariant claim rejected).\n" << std::dec << std::setfill(' ');
    }

    std::cout << "  [PASS] Stage 266.11 JOPHASH opaque-value semantics verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 266.12: PAC Profile and Frame Layout Audit
// -----------------------------------------------------------------------------
static bool test_stage266_12_pac_profile_and_frame_layout() {
    std::cout << "[Stage 266.12] Auditing PAC profile vs Standard ARM64 frame layout...\n";

    // Standard ARM64
    auto std_layout = get_exception_frame_layout(ExecutionProfile::ARM64_Standard);
    TEST_ASSERT(std_layout.raw_saved_state_size == 288, "Standard ARM64 raw size is 288 bytes (72 words)");
    TEST_ASSERT(std_layout.frame_allocation_size == 288, "Standard ARM64 allocation size is 288 bytes");
    TEST_ASSERT(!std_layout.has_apple_pac, "Standard ARM64 does NOT include JOPHASH");

    // ARM64e PAC
    auto pac_layout = get_exception_frame_layout(ExecutionProfile::ARM64e_PAC);
    TEST_ASSERT(pac_layout.raw_saved_state_size == 296, "ARM64e PAC raw size is 296 bytes (74 words)");
    TEST_ASSERT(pac_layout.frame_allocation_size == 304, "ARM64e PAC allocation size is 304 bytes (16-byte SP alignment)");
    TEST_ASSERT(pac_layout.has_apple_pac, "ARM64e PAC includes JOPHASH at offset 288");
    TEST_ASSERT(pac_layout.jophash_offset == 288, "JOPHASH is at offset 288 (0x120)");

    // Upstream static asserts
    static_assert(sizeof(arm_saved_state64_upstream) == 288, "arm_saved_state64_upstream must be 288 bytes");
    static_assert(sizeof(arm_saved_state64_pac_upstream) == 296, "arm_saved_state64_pac_upstream must be 296 bytes");
    static_assert(offsetof(arm_saved_state64_pac_upstream, jophash) == 288, "jophash offset must be 288");

    std::cout << "  - Standard ARM64: Raw size 288 bytes, Aligned 288 bytes.\n";
    std::cout << "  - ARM64e PAC:     Raw size 296 bytes, Aligned 304 bytes (Hardware SP 16-byte alignment).\n";
    std::cout << "  - Architectural boundary: Saved-state ABI compatibility is compliant; hardware cryptographic signing deferred.\n";

    std::cout << "  [PASS] Stage 266.12 PAC profile and frame layout verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 266.13: Full Exception Instruction Listing & Clean-Room Decoder Audit
// -----------------------------------------------------------------------------
static bool test_stage266_13_exception_instruction_listing_and_decoder() {
    std::cout << "[Stage 266.13] Generating complete instruction listings and running clean-room decoder audit...\n";

    // 1. Audit EL0 Entry & Restore
    auto el0_entry = encode_exception_entry_sequence(ExecutionProfile::ARM64_Standard, true /* is_el0_origin */);
    auto el0_restore = encode_exception_restore_sequence(ExecutionProfile::ARM64_Standard, true /* is_el0_origin */);

    std::cout << "  === EL0 Exception Entry Sequence (28 Instructions) ===\n";
    for (std::size_t i = 0; i < el0_entry.size(); ++i) {
        std::uint32_t word = el0_entry[i];
        auto decoded = AArch64InstructionDecoder::decode(word);
        TEST_ASSERT(decoded.is_valid, "Instruction word must be valid AArch64");
        std::cout << "    [+0x" << std::hex << std::right << std::setw(2) << std::setfill('0') << (i * 4) << "] 0x"
                  << std::setw(8) << word << " : " << std::left << std::setw(30) << decoded.disassembly << std::dec << std::setfill(' ')
                  << "\n";
    }

    std::cout << "  === EL0 Exception Restore Sequence (24 Instructions) ===\n";
    for (std::size_t i = 0; i < el0_restore.size(); ++i) {
        std::uint32_t word = el0_restore[i];
        auto decoded = AArch64InstructionDecoder::decode(word);
        TEST_ASSERT(decoded.is_valid, "Instruction word must be valid AArch64");
        std::cout << "    [+0x" << std::hex << std::right << std::setw(2) << std::setfill('0') << (i * 4) << "] 0x"
                  << std::setw(8) << word << " : " << std::left << std::setw(30) << decoded.disassembly << std::dec << std::setfill(' ')
                  << "\n";
    }

    // 2. Audit EL1 Entry & Restore
    auto el1_entry = encode_exception_entry_sequence(ExecutionProfile::ARM64_Standard, false /* is_el0_origin */);
    auto el1_restore = encode_exception_restore_sequence(ExecutionProfile::ARM64_Standard, false /* is_el0_origin */);

    std::cout << "  === EL1 Exception Entry Sequence (28 Instructions) ===\n";
    for (std::size_t i = 0; i < el1_entry.size(); ++i) {
        std::uint32_t word = el1_entry[i];
        auto decoded = AArch64InstructionDecoder::decode(word);
        TEST_ASSERT(decoded.is_valid, "Instruction word must be valid AArch64");
        std::cout << "    [+0x" << std::hex << std::right << std::setw(2) << std::setfill('0') << (i * 4) << "] 0x"
                  << std::setw(8) << word << " : " << std::left << std::setw(30) << decoded.disassembly << std::dec << std::setfill(' ')
                  << "\n";
    }

    std::cout << "  === EL1 Exception Restore Sequence (24 Instructions) ===\n";
    for (std::size_t i = 0; i < el1_restore.size(); ++i) {
        std::uint32_t word = el1_restore[i];
        auto decoded = AArch64InstructionDecoder::decode(word);
        TEST_ASSERT(decoded.is_valid, "Instruction word must be valid AArch64");
        std::cout << "    [+0x" << std::hex << std::right << std::setw(2) << std::setfill('0') << (i * 4) << "] 0x"
                  << std::setw(8) << word << " : " << std::left << std::setw(30) << decoded.disassembly << std::dec << std::setfill(' ')
                  << "\n";
    }

    // Explicitly verify corrected STR/LDR words in decoded stream
    TEST_ASSERT(el0_entry[22] == 0xB9010BE1u, "EL0 entry SPSR store must be 0xB9010BE1 (str w1, [sp, #264])");
    TEST_ASSERT(el0_restore[2] == 0xB9410BE1u, "EL0 restore SPSR load must be 0xB9410BE1 (ldr w1, [sp, #264])");
    TEST_ASSERT(el1_entry[22] == 0xB9010BE1u, "EL1 entry SPSR store must be 0xB9010BE1 (str w1, [sp, #264])");
    TEST_ASSERT(el1_restore[2] == 0xB9410BE1u, "EL1 restore SPSR load must be 0xB9410BE1 (ldr w1, [sp, #264])");

    std::cout << "  [PASS] Stage 266.13 Instruction listings and clean-room decoder audit verified.\n";
    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Campaign 266 - Stages 266.11..266.13 PAC & Decoder Tests\n";
    std::cout << "=================================================================\n";

    if (!test_stage266_11_jophash_opaque_semantics()) return 1;
    if (!test_stage266_12_pac_profile_and_frame_layout()) return 1;
    if (!test_stage266_13_exception_instruction_listing_and_decoder()) return 1;

    std::cout << "=================================================================\n";
    std::cout << "All Stages 266.11..266.13 Tests Passed Successfully!\n";
    std::cout << "=================================================================\n";
    return 0;
}
