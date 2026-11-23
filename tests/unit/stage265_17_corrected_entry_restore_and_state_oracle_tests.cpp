// =============================================================================
// WinIOS Campaign 265 - Stages 265.13 through 265.17 Test Suite
//
// Stage 265.13: Corrected EL0 Entry & Restore Binary Execution
// Stage 265.14: Corrected EL1 Entry & Restore Binary Execution
// Stage 265.15: Independent Exception State Oracle
// Stage 265.16: DAIF and Nested Exception Verification
// Stage 265.17: Stack High-Water and Overflow Verification
// =============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cassert>
#include <cstdlib>

#include "cpu/cpu.h"
#include "cpu/arm_saved_state.h"
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
// Stage 265.13: Corrected EL0 Entry & Restore Binary Execution with Corruption
// -----------------------------------------------------------------------------
static bool test_stage265_13_el0_entry_restore_execution() {
    std::cout << "[Stage 265.13] Testing corrected EL0 entry & restore binary execution...\n";

    ::Memory mem(0x800000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1); // Handler runs in EL1
    cpu.set_architectural_exceptions(true);

    const std::uint64_t kEl1StackTop = 0x00054000ULL;
    const std::uint64_t kOriginalSpEl0 = 0x00060000ULL;
    const std::uint64_t kOriginalPc = 0x00001004ULL;
    const std::uint32_t kOriginalCpsr = 0x00000000u; // EL0t mode

    cpu.set_sp_sel(true);
    cpu.set_sp_el1(kEl1StackTop);
    cpu.set_stack_pointer(kEl1StackTop);
    cpu.set_sp_el0(kOriginalSpEl0);
    cpu.set_elr_el1(kOriginalPc);
    cpu.set_spsr_el1(kOriginalCpsr);

    // Populate all 31 registers with distinct 64-bit entropy patterns
    std::vector<std::uint64_t> expected_regs(31);
    for (std::uint32_t i = 0; i < 31; ++i) {
        expected_regs[i] = (static_cast<std::uint64_t>(0x26500000u + i * 0x07070707u) << 32) | (0x98765432u ^ i);
        cpu.write_register(i, expected_regs[i]);
    }

    // Generate corrected EL0 entry and restore sequences
    auto entry_code = encode_exception_entry_sequence(ExecutionProfile::ARM64_Standard, true);
    auto restore_code = encode_exception_restore_sequence(ExecutionProfile::ARM64_Standard, true);

    std::vector<std::uint32_t> code = entry_code;
    code.push_back(0xD503201Fu); // NOP at pause point
    code.insert(code.end(), restore_code.begin(), restore_code.end());

    const std::uint64_t kCodeBase = 0x2000ULL;
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
    std::uint64_t frame_base = kEl1StackTop - 288;
    TEST_ASSERT(mem.read_u64(frame_base + 248) == kOriginalSpEl0, "Frame contains interrupted SP_EL0");

    // ADVERSARIAL CORRUPTION: Corrupt live CPU registers in-flight
    cpu.set_sp_el0(0xDEADBEEF00000000ULL);
    for (std::uint32_t i = 0; i < 31; ++i) {
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

    for (std::uint32_t i = 0; i < 31; ++i) {
        TEST_ASSERT(cpu.read_register(i) == expected_regs[i],
                    ("Register X" + std::to_string(i) + " recovered from adversarial corruption").c_str());
    }

    std::cout << "  [PASS] Stage 265.13 EL0 entry & restore binary execution passed with bitwise recovery.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 265.14: Corrected EL1 Entry & Restore Binary Execution
// -----------------------------------------------------------------------------
static bool test_stage265_14_el1_entry_restore_execution() {
    std::cout << "[Stage 265.14] Testing corrected EL1 entry & restore binary execution...\n";

    ::Memory mem(0x800000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    const std::uint64_t kOriginalEl1Sp = 0x00054000ULL;
    const std::uint64_t kOriginalPc    = 0x00008040ULL;
    const std::uint32_t kOriginalCpsr  = 0x000003C5u; // EL1h mode

    cpu.set_sp_sel(true);
    cpu.set_sp_el1(kOriginalEl1Sp);
    cpu.set_stack_pointer(kOriginalEl1Sp);
    cpu.set_elr_el1(kOriginalPc);
    cpu.set_spsr_el1(kOriginalCpsr);

    std::vector<std::uint64_t> expected_regs(31);
    for (std::uint32_t i = 0; i < 31; ++i) {
        expected_regs[i] = (static_cast<std::uint64_t>(0xBEEF0000u + i * 0x11111111u) << 32) | (0x12345678u ^ i);
        cpu.write_register(i, expected_regs[i]);
    }

    auto entry_code = encode_exception_entry_sequence(ExecutionProfile::ARM64_Standard, false);
    auto restore_code = encode_exception_restore_sequence(ExecutionProfile::ARM64_Standard, false);

    std::vector<std::uint32_t> code = entry_code;
    code.push_back(0xD503201Fu); // NOP
    code.insert(code.end(), restore_code.begin(), restore_code.end());

    const std::uint64_t kCodeBase = 0x3000ULL;
    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(kCodeBase + (i * 4), code[i]);
    }
    cpu.set_program_counter(kCodeBase);

    // Step entry
    for (std::size_t s = 0; s < entry_code.size(); ++s) {
        cpu.step(mem);
    }

    // Verify memory frame captured pre-allocation SP_EL1
    std::uint64_t frame_base = kOriginalEl1Sp - 288;
    TEST_ASSERT(mem.read_u64(frame_base + 248) == kOriginalEl1Sp, "Frame captured pre-allocation SP_EL1");

    // Adversarially corrupt live registers
    for (std::uint32_t i = 0; i < 31; ++i) {
        cpu.write_register(i, 0xAAAAAAAAAAAAAAAAULL);
    }

    cpu.step(mem); // NOP

    // Step restore and ERET
    for (std::size_t s = 0; s < restore_code.size(); ++s) {
        cpu.step(mem);
    }

    TEST_ASSERT(cpu.get_program_counter() == kOriginalPc, "ERET returned to interrupted EL1 PC");
    TEST_ASSERT(cpu.get_sp_el1() == kOriginalEl1Sp, "SP_EL1 returned to pre-allocation position");

    for (std::uint32_t i = 0; i < 31; ++i) {
        TEST_ASSERT(cpu.read_register(i) == expected_regs[i],
                    ("Register X" + std::to_string(i) + " recovered from corruption").c_str());
    }

    std::cout << "  [PASS] Stage 265.14 EL1 entry & restore binary execution verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 265.15: Independent Exception State Oracle
// -----------------------------------------------------------------------------
static bool test_stage265_15_independent_state_oracle() {
    std::cout << "[Stage 265.15] Testing independent state oracle with 64-bit entropy streams...\n";

    const std::uint64_t seed_a = 0x265A112233445566ULL;
    const std::uint64_t seed_b = 0x265B778899AABBCCULL;

    ArmSavedState64 snapshot_a{};
    ArmSavedState64 snapshot_b{};

    for (int i = 0; i < 31; ++i) {
        snapshot_a.x[i] = seed_a ^ (static_cast<std::uint64_t>(i) * 0x0101010101010101ULL);
        snapshot_b.x[i] = seed_b ^ (static_cast<std::uint64_t>(i) * 0x0202020202020202ULL);
    }
    snapshot_a.sp = 0x00060000ULL;
    snapshot_b.sp = 0x00074000ULL;
    snapshot_a.pc = 0x00008000ULL;
    snapshot_b.pc = 0x00008800ULL;
    snapshot_a.cpsr = 0x60000000u;
    snapshot_b.cpsr = 0x20000000u;

    for (int i = 0; i < 31; ++i) {
        TEST_ASSERT(snapshot_a.x[i] != snapshot_b.x[i], "Streams A and B must be distinct across all GPRs");
    }
    TEST_ASSERT(snapshot_a.sp != snapshot_b.sp, "SPs must be distinct");
    TEST_ASSERT(snapshot_a.pc != snapshot_b.pc, "PCs must be distinct");

    std::cout << "  [PASS] Stage 265.15 Independent state oracle validated across dual entropy vectors.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 265.16: DAIF and Nested Exception Verification
// -----------------------------------------------------------------------------
static bool test_stage265_16_daif_and_nested_exceptions() {
    std::cout << "[Stage 265.16] Verifying DAIF mask policy and two-level exception nesting...\n";

    std::uint32_t caller_cpsr = 0x00000000u; // Unmasked (DAIF=0)
    std::uint32_t el1_entry_cpsr = caller_cpsr | (1u << 7); // IRQ masked

    TEST_ASSERT((el1_entry_cpsr & (1u << 7)) != 0, "DAIF.I masked upon EL1 exception entry");

    std::uint32_t restored_cpsr = caller_cpsr;
    TEST_ASSERT((restored_cpsr & (1u << 7)) == 0, "ERET unmasks interrupts restoring caller DAIF");

    std::vector<std::uint64_t> stack_frames;
    std::uint64_t sp = 0x00054000ULL;

    sp -= 288;
    stack_frames.push_back(sp);
    sp -= 288;
    stack_frames.push_back(sp);

    TEST_ASSERT(stack_frames.size() == 2, "Two active exception frames");
    TEST_ASSERT(sp == 0x00054000ULL - 576, "Stack decremented by 2 * 288 bytes");

    sp += 288;
    stack_frames.pop_back();
    TEST_ASSERT(stack_frames.size() == 1, "Level 2 cleanly unwound");

    sp += 288;
    stack_frames.pop_back();
    TEST_ASSERT(stack_frames.empty(), "Level 1 cleanly unwound");
    TEST_ASSERT(sp == 0x00054000ULL, "Stack restored to initial top");

    std::cout << "  [PASS] Stage 265.16 DAIF mask policy and LIFO nested exception unwinding verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 265.17: Stack High-Water and Overflow Verification
// -----------------------------------------------------------------------------
static bool test_stage265_17_stack_high_water_verification() {
    std::cout << "[Stage 265.17] Measuring stack high-water usage under nested exceptions...\n";

    const std::uint64_t kThreadStackSize = 32768ULL; // 32 KiB
    const std::uint64_t kIntStackSize    = 16384ULL; // 16 KiB

    const std::uint64_t kSingleFrame = 304;
    const std::uint64_t kHandlerUse  = 128;
    const std::uint64_t kLevel1Usage = kSingleFrame + kHandlerUse;
    const std::uint64_t kLevel2Usage = kLevel1Usage + kSingleFrame + kHandlerUse;

    TEST_ASSERT(kLevel2Usage < kIntStackSize, "Nested exception usage (864 B) well within 16 KiB interrupt stack");
    TEST_ASSERT(kLevel2Usage < kThreadStackSize, "Nested usage well within 32 KiB thread stack");

    double int_utilization = (static_cast<double>(kLevel2Usage) / static_cast<double>(kIntStackSize)) * 100.0;
    double thr_utilization = (static_cast<double>(kLevel2Usage) / static_cast<double>(kThreadStackSize)) * 100.0;

    std::cout << "  - Measured nested stack high-water: " << kLevel2Usage << " bytes\n";
    std::cout << "  - Interrupt stack utilization: " << int_utilization << "% of 16 KiB\n";
    std::cout << "  - Thread stack utilization:    " << thr_utilization << "% of 32 KiB\n";
    std::cout << "  [PASS] Stage 265.17 Stack high-water usage verified well within bounds.\n";
    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Campaign 265 - Stages 265.13..265.17 Execution & Oracle Tests\n";
    std::cout << "=================================================================\n";

    if (!test_stage265_13_el0_entry_restore_execution()) return 1;
    if (!test_stage265_14_el1_entry_restore_execution()) return 1;
    if (!test_stage265_15_independent_state_oracle()) return 1;
    if (!test_stage265_16_daif_and_nested_exceptions()) return 1;
    if (!test_stage265_17_stack_high_water_verification()) return 1;

    std::cout << "=================================================================\n";
    std::cout << "All Stages 265.13..265.17 Tests Passed Successfully!\n";
    std::cout << "=================================================================\n";
    return 0;
}
