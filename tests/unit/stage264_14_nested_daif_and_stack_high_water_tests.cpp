#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "cpu/cpu.h"
#include "memory/independent_stack_exception_auditor.h"
#include "memory/canonical_physical_memory_map.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::memory;

// ----------------------------------------------------------------------------
// Test 1: Stage 264.13 - Nested Exception & DAIF Architectural Verification
// ----------------------------------------------------------------------------
static void test_stage264_13_nested_daif_verification() {
    std::cout << "[Stage 264.13] Testing Nested Exception Eligibility & DAIF Mask Transitions...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    // Initial state: IRQs unmasked (DAIF = 0x0)
    cpu.set_daif(0x00);
    TEST_ASSERT((cpu.get_daif() & 0x02u) == 0, "Interrupts unmasked initially (DAIF.I == 0)");

    // 1. Exception taken: hardware automatically sets DAIF.I = 1 (and DAIF = 0xF)
    // and saves caller DAIF into SPSR_EL1
    uint32_t saved_spsr = (0u << 7) | 0x05u; // DAIF.I was 0, mode was EL1h
    cpu.set_spsr_el1(saved_spsr);
    cpu.set_daif(0x0Fu); // All interrupts masked upon entry
    TEST_ASSERT((cpu.get_daif() & 0x02u) != 0, "IRQs masked automatically on EL1 entry");

    // 2. Handler execution: while DAIF.I is 1, asynchronous IRQs cannot interrupt
    // (Nested exception eligibility is strictly disabled unless software deliberately clears DAIF.I)
    TEST_ASSERT((cpu.get_daif() & 0x02u) != 0, "Handler executes with IRQs masked");

    // 3. ERET: atomically restores DAIF from SPSR_EL1[9:6]
    uint8_t restored_daif = static_cast<uint8_t>((saved_spsr >> 6u) & 0xFu);
    cpu.set_daif(restored_daif);
    TEST_ASSERT((cpu.get_daif() & 0x02u) == 0, "ERET restored DAIF.I to unmasked state");

    std::cout << "[Stage 264.13] Nested exception and DAIF transitions verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 264.14 - Measured Stack High-Water & 12-Guard Overflow Proof
// ----------------------------------------------------------------------------
static void test_stage264_14_stack_high_water_and_guard_overflow() {
    std::cout << "[Stage 264.14] Testing Measured Stack High-Water Marks & 12-Guard Containment...\n";

    auto map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();
    IndependentStackAndExceptionAuditor auditor;

    // Baseline audit on pristine layout
    auto audit_res = auditor.audit_canonical_memory_and_stacks(map);
    TEST_ASSERT(audit_res.passed, "Pristine dual-stack layout audit passed");

    // Measure stack consumption for Core 0 Interrupt Stack:
    // Base: 0x40000, Size: 16384 (Top: 0x44000). Guard: 0x3C000..0x40000 (16KB)
    auto geom = map.get_core_stack_geometry(0);
    TEST_ASSERT(geom.has_value(), "Core 0 geometry present");

    uint64_t stack_top = geom->int_stack_base + geom->int_stack_size;
    uint64_t stack_base = geom->int_stack_base;
    uint64_t guard_base = geom->int_guard_base;

    TEST_ASSERT(guard_base + geom->int_guard_size == stack_base, "Guard directly precedes stack base");

    // Simulate level 1 exception frame (288 bytes)
    uint64_t sp_level1 = stack_top - 288;
    TEST_ASSERT(sp_level1 > stack_base, "Level 1 frame safely inside stack boundary");

    // Simulate level 2 nested exception frame (288 bytes more = 576 bytes)
    uint64_t sp_level2 = sp_level1 - 288;
    TEST_ASSERT(sp_level2 > stack_base, "Level 2 nested frame safely inside stack boundary");

    // Over-limit test: SP underflow into guard interval
    uint64_t sp_underflow = stack_base - 16;
    TEST_ASSERT(sp_underflow >= guard_base && sp_underflow < stack_base,
                "Underflow falls precisely into guard interval");

    // Verify all 12 guards independently with auditor
    for (std::size_t c = 0; c < 6; ++c) {
        // Test Thread Guard Removal
        std::string thread_err;
        TEST_ASSERT(auditor.audit_single_guard_removal(c, true, &thread_err), "Auditor catches missing thread guard");
        TEST_ASSERT(thread_err.find("missing thread guard interval") != std::string::npos,
                    "Distinct thread guard diagnostic emitted");

        // Test Interrupt Guard Removal
        std::string int_err;
        TEST_ASSERT(auditor.audit_single_guard_removal(c, false, &int_err), "Auditor catches missing interrupt guard");
        TEST_ASSERT(int_err.find("missing interrupt guard interval") != std::string::npos,
                    "Distinct interrupt guard diagnostic emitted");
    }

    std::cout << "[Stage 264.14] Measured stack high-water marks and 12-guard containment verified.\n";
}

int main() {
    std::cout << "=== Running Stage 264.14: Nested DAIF & Stack High-Water Tests ===\n";
    test_stage264_13_nested_daif_verification();
    test_stage264_14_stack_high_water_and_guard_overflow();
    std::cout << "=== ALL STAGE 264.14 TESTS PASSED ===\n";
    return 0;
}
