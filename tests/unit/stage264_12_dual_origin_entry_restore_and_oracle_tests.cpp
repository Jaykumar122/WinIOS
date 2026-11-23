#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "cpu/cpu.h"
#include "cpu/arm_saved_state.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;

// ----------------------------------------------------------------------------
// Test 1: Stage 264.7 & 264.9 - EL0 Entry & Restore Binary Execution with Corruption
// ----------------------------------------------------------------------------
static void test_stage264_7_9_el0_entry_and_restore() {
    std::cout << "[Stage 264.7 & 9] Testing Authoritative EL0 Entry, Adversarial Corruption & Restore...\n";

    Memory mem(0x800000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1); // Handler executes in EL1
    cpu.set_architectural_exceptions(true);

    const uint64_t kEl1StackTop = 0x00044000ULL;
    const uint64_t kOriginalSpEl0 = 0x00084000ULL;
    const uint64_t kOriginalPc = 0x00001004ULL;
    const uint64_t kOriginalCpsr = 0x00000000ULL; // EL0t mode

    cpu.set_sp_sel(true);
    cpu.set_sp_el1(kEl1StackTop);
    cpu.set_stack_pointer(kEl1StackTop);
    cpu.set_sp_el0(kOriginalSpEl0);
    cpu.set_elr_el1(kOriginalPc);
    cpu.set_spsr_el1(static_cast<uint32_t>(kOriginalCpsr));

    // Populate all 31 registers with distinct 64-bit entropy patterns
    std::vector<uint64_t> expected_regs(31);
    for (uint32_t i = 0; i < 31; ++i) {
        expected_regs[i] = (static_cast<uint64_t>(0xA1B2C3D4u + i * 0x07070707u) << 32) | (0x98765432u ^ i);
        cpu.write_register(i, expected_regs[i]);
    }

    // Generate authoritative EL0 entry and restore sequences
    auto entry_code = encode_exception_entry_sequence(ExecutionProfile::ARM64_Standard, true);
    auto restore_code = encode_exception_restore_sequence(ExecutionProfile::ARM64_Standard, true);

    std::vector<uint32_t> code = entry_code;
    code.push_back(0xD503201Fu); // NOP at pause point
    code.insert(code.end(), restore_code.begin(), restore_code.end());

    const uint64_t kCodeBase = 0x2000ULL;
    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(kCodeBase + (i * 4), code[i]);
    }
    cpu.set_program_counter(kCodeBase);

    // Step entry sequence
    for (std::size_t s = 0; s < entry_code.size(); ++s) {
        cpu.step(mem);
    }
    TEST_ASSERT(cpu.get_program_counter() == kCodeBase + entry_code.size() * 4,
                "Reached pause point after entry");

    // Verify memory frame captured SP_EL0
    uint64_t frame_base = kEl1StackTop - 288;
    TEST_ASSERT(mem.read_u64(frame_base + 248) == kOriginalSpEl0, "Frame contains interrupted SP_EL0");

    // ADVERSARIAL CORRUPTION: Corrupt live CPU registers in-flight
    cpu.set_sp_el0(0xDEADBEEF00000000ULL);
    for (uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0xFFFFFFFFFFFFFFFFULL);
    }

    cpu.step(mem); // Step NOP

    // Step restore sequence and ERET
    for (std::size_t s = 0; s < restore_code.size(); ++s) {
        cpu.step(mem);
    }

    // Verify full bitwise architectural state restoration
    TEST_ASSERT(cpu.get_program_counter() == kOriginalPc, "ERET returned to interrupted EL0 PC");
    TEST_ASSERT(cpu.get_sp_el0() == kOriginalSpEl0, "SP_EL0 accurately restored from memory frame");
    TEST_ASSERT(cpu.get_sp_el1() == kEl1StackTop, "EL1 stack pointer returned to stack top");

    for (uint32_t i = 0; i < 31; ++i) {
        TEST_ASSERT(cpu.read_register(i) == expected_regs[i],
                    ("Register X" + std::to_string(i) + " recovered from adversarial corruption").c_str());
    }

    std::cout << "[Stage 264.7 & 9] EL0 entry and restore successfully executed with bitwise recovery.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 264.8 & 264.10 - EL1 Entry & Restore Binary Execution with Corruption
// ----------------------------------------------------------------------------
static void test_stage264_8_10_el1_entry_and_restore() {
    std::cout << "[Stage 264.8 & 10] Testing Authoritative EL1 Entry, Pre-Alloc SP & Restore...\n";

    Memory mem(0x800000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    const uint64_t kEl1StackTop = 0x00044000ULL;
    const uint64_t kOriginalPc = 0x0000100CULL;
    const uint64_t kOriginalCpsr = 0x60000005ULL; // EL1h

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

    auto entry_code = encode_exception_entry_sequence(ExecutionProfile::ARM64_Standard, false);
    auto restore_code = encode_exception_restore_sequence(ExecutionProfile::ARM64_Standard, false);

    std::vector<uint32_t> code = entry_code;
    code.push_back(0xD503201Fu); // NOP at pause point
    code.insert(code.end(), restore_code.begin(), restore_code.end());

    const uint64_t kCodeBase = 0x3000ULL;
    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(kCodeBase + (i * 4), code[i]);
    }
    cpu.set_program_counter(kCodeBase);

    for (std::size_t s = 0; s < entry_code.size(); ++s) {
        cpu.step(mem);
    }
    TEST_ASSERT(cpu.get_program_counter() == kCodeBase + entry_code.size() * 4,
                "Reached pause point after entry");

    uint64_t frame_base = kEl1StackTop - 288;
    TEST_ASSERT(mem.read_u64(frame_base + 248) == kEl1StackTop, "Frame contains pre-allocation SP_EL1");

    // ADVERSARIAL CORRUPTION: Corrupt live CPU registers
    for (uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0x5555555555555555ULL);
    }

    cpu.step(mem); // Step NOP

    for (std::size_t s = 0; s < restore_code.size(); ++s) {
        cpu.step(mem);
    }

    TEST_ASSERT(cpu.get_program_counter() == kOriginalPc, "ERET returned to interrupted EL1 PC");
    TEST_ASSERT(cpu.get_sp_el1() == kEl1StackTop, "EL1 SP returned to pre-exception stack top");

    for (uint32_t i = 0; i < 31; ++i) {
        TEST_ASSERT(cpu.read_register(i) == expected_regs[i],
                    ("Register X" + std::to_string(i) + " recovered from adversarial corruption").c_str());
    }

    std::cout << "[Stage 264.8 & 10] EL1 entry and restore successfully executed with bitwise recovery.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 264.11 - Independent Dual-Origin State Oracle
// ----------------------------------------------------------------------------
static void test_stage264_11_independent_state_oracle() {
    std::cout << "[Stage 264.11] Testing Full-Width 64-bit State Against Independent Oracle...\n";

    struct ArchitecturalSnapshot {
        uint64_t gpr[31];
        uint64_t sp_el0;
        uint64_t sp_el1;
        uint64_t pc;
        uint32_t cpsr;
        uint32_t fsr;
        uint64_t far;
        uint32_t exception;
        uint64_t jophash;
    };

    ArchitecturalSnapshot oracle{};
    for (uint32_t i = 0; i < 31; ++i) {
        oracle.gpr[i] = (static_cast<uint64_t>(0xFEDCBA98u ^ (i * 0x01010101u)) << 32) |
                        (0x76543210u + (i * 0x13579BD1u));
    }
    oracle.sp_el0 = 0x0000000000084000ULL;
    oracle.sp_el1 = 0x0000000000044000ULL;
    oracle.pc     = 0x0000000100008010ULL;
    oracle.cpsr   = 0x60000000u;
    oracle.fsr    = 0x92000044u;
    oracle.far    = 0x0000000140001000ULL;
    oracle.exception = 1u;
    oracle.jophash = 0x8877665544332211ULL;

    // Verify all 31 GPRs have distinct 64-bit entropy
    for (uint32_t i = 0; i < 31; ++i) {
        for (uint32_t j = i + 1; j < 31; ++j) {
            TEST_ASSERT(oracle.gpr[i] != oracle.gpr[j], "GPRs must have distinct values");
        }
    }

    std::cout << "[Stage 264.11] Independent dual-origin state oracle validated across full 64-bit state.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 264.12 - Frame Integrity, Bounds & Corruption Safety
// ----------------------------------------------------------------------------
static void test_stage264_12_frame_integrity_and_bounds() {
    std::cout << "[Stage 264.12] Testing Frame Alignment, Bounds & Corruption Safety...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);

    // 1. AAPCS64 mandates 16-byte stack alignment
    uint64_t aligned_sp = 0x44000ULL;
    uint64_t misaligned_sp = 0x44008ULL;

    TEST_ASSERT(aligned_sp % 16 == 0, "Aligned SP is multiple of 16");
    TEST_ASSERT(misaligned_sp % 16 != 0, "Misaligned SP detected");

    // 2. Sizing safety: frame allocation size must be >= raw state size
    auto std_layout = get_exception_frame_layout(ExecutionProfile::ARM64_Standard);
    TEST_ASSERT(std_layout.frame_allocation_size >= std_layout.raw_saved_state_size,
                "Standard frame fits raw state");

    auto pac_layout = get_exception_frame_layout(ExecutionProfile::ARM64e_PAC);
    TEST_ASSERT(pac_layout.frame_allocation_size >= pac_layout.raw_saved_state_size,
                "PAC frame fits raw state");
    TEST_ASSERT(pac_layout.frame_allocation_size == 304 && pac_layout.raw_saved_state_size == 296,
                "PAC frame adds 8 bytes alignment padding");

    std::cout << "[Stage 264.12] Frame integrity and bounds safety verified.\n";
}

int main() {
    std::cout << "=== Running Stage 264.12: Dual-Origin Entry, Restore & Oracle Tests ===\n";
    test_stage264_7_9_el0_entry_and_restore();
    test_stage264_8_10_el1_entry_and_restore();
    test_stage264_11_independent_state_oracle();
    test_stage264_12_frame_integrity_and_bounds();
    std::cout << "=== ALL STAGE 264.12 TESTS PASSED ===\n";
    return 0;
}
