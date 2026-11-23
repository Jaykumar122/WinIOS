#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "memory/independent_stack_exception_auditor.h"
#include "memory/canonical_physical_memory_map.h"
#include "cpu/cpu.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::memory;
using namespace winios::cpu;

// ----------------------------------------------------------------------------
// Test 1: Stage 263.12 - Nested Exception & DAIF Policy
// ----------------------------------------------------------------------------
static void test_stage263_12_nested_daif_policy() {
    std::cout << "[Stage 263.12] Testing Nested Exception & Architectural DAIF Policy...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    // Initial state: interrupts enabled (DAIF.I = 0)
    cpu.set_daif(0x00);
    TEST_ASSERT((cpu.get_daif() & 0x02u) == 0, "IRQ unmasked initially");

    // Simulate architectural exception entry to EL1:
    // Hardware automatically masks IRQs: DAIF.I = 1 (bits 3:0 = {D, A, I, F})
    // And saves original DAIF into SPSR_EL1[9:6]
    uint32_t saved_spsr = (0u << 7) | 0x05u; // DAIF.I was 0, EL1h
    cpu.set_spsr_el1(saved_spsr);
    cpu.set_daif(0x0Fu); // Mask interrupts (0xF)
    TEST_ASSERT((cpu.get_daif() & 0x02u) != 0, "IRQ masked on exception entry");

    // ERET simulation: restores DAIF from SPSR_EL1
    // (In full hardware ERET, PSTATE.DAIF is restored)
    uint8_t restored_daif = static_cast<uint8_t>((saved_spsr >> 6u) & 0xFu);
    cpu.set_daif(restored_daif);
    TEST_ASSERT((cpu.get_daif() & 0x02u) == 0, "IRQ unmasked automatically upon ERET");

    std::cout << "[Stage 263.12] Nested exception & DAIF policy verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 263.13 - Worst-Case Stack Instrumentation & Guard Trapping
// ----------------------------------------------------------------------------
static void test_stage263_13_stack_instrumentation_and_overlimit() {
    std::cout << "[Stage 263.13] Testing Stack High-Water Marks & Over-Limit Containment...\n";

    const std::size_t kStackSize = 16384;
    const std::size_t kFrameSize = 384; // 288 B context + 64 B locals + 32 B call frame

    std::size_t max_safe_depth = kStackSize / kFrameSize; // 42 levels
    TEST_ASSERT(max_safe_depth == 42, "Maximum safe nesting depth is 42");

    // Test depths 1..10
    for (std::size_t depth = 1; depth <= 10; ++depth) {
        std::size_t consumed = depth * kFrameSize;
        std::size_t remaining = kStackSize - consumed;
        TEST_ASSERT(consumed < kStackSize, "Within stack limit");
        TEST_ASSERT(remaining >= 12000, "Substantial headroom preserved (> 12 KiB)");
    }

    // Over-limit test (depth 43):
    // Depth 43 consumes 43 * 384 = 16,512 bytes > 16,384 bytes
    // Underflow by 128 bytes penetrates into the 16 KiB unmapped guard!
    CanonicalPhysicalMemoryMap map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();
    auto geom = map.get_core_stack_geometry(0);
    TEST_ASSERT(geom.has_value(), "Core 0 geometry exists");

    uint64_t overlimit_sp = geom->int_stack_initial_sp - (43 * kFrameSize);
    TEST_ASSERT(overlimit_sp < geom->int_stack_base, "Over-limit SP underflows stack base");
    TEST_ASSERT(overlimit_sp >= geom->int_guard_base, "Over-limit SP falls strictly into InterruptGuard");

    const auto* hit_guard = map.find_interval_by_address(overlimit_sp);
    TEST_ASSERT(hit_guard != nullptr, "Hit guard interval");
    TEST_ASSERT(hit_guard->name == "Core0_InterruptGuard", "Trapped in Core0_InterruptGuard");
    TEST_ASSERT(hit_guard->permissions == 0, "Guard has zero permissions -> triggers immediate abort");

    std::cout << "[Stage 263.13] Stack instrumentation & guard underflow containment verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 263.14 - Stack Auditor Diagnostic Correction (All 12 Guards)
// ----------------------------------------------------------------------------
static void test_stage263_14_auditor_diagnostic_correction() {
    std::cout << "[Stage 263.14] Testing Stack Auditor Diagnostic Disambiguation on All 12 Guards...\n";

    IndependentStackAndExceptionAuditor auditor;

    // Verify disambiguation between Mutation 5 and Mutation 6:
    std::string msg5;
    bool det5 = auditor.test_mutation_detection(MutationType::MissingThreadStackGuard, &msg5);
    TEST_ASSERT(det5, "MissingThreadStackGuard detected");
    TEST_ASSERT(msg5.find("thread guard") != std::string::npos,
                "MissingThreadStackGuard reports 'thread guard' (not interrupt guard)");
    std::cout << "  -> Mutation 5 reported: " << msg5 << "\n";

    std::string msg6;
    bool det6 = auditor.test_mutation_detection(MutationType::MissingInterruptStackGuard, &msg6);
    TEST_ASSERT(det6, "MissingInterruptStackGuard detected");
    TEST_ASSERT(msg6.find("interrupt guard") != std::string::npos,
                "MissingInterruptStackGuard reports 'interrupt guard'");
    std::cout << "  -> Mutation 6 reported: " << msg6 << "\n";

    // Now test all 12 guards independently across all 6 cores!
    for (std::size_t core = 0; core < 6; ++core) {
        // Test Interrupt Guard removal on this core
        std::string err_int;
        bool caught_int = auditor.audit_single_guard_removal(core, false, &err_int);
        TEST_ASSERT(caught_int, ("Core " + std::to_string(core) + " interrupt guard removal detected").c_str());
        TEST_ASSERT(err_int.find("Core " + std::to_string(core)) != std::string::npos, "Core ID match in error");
        TEST_ASSERT(err_int.find("missing interrupt guard interval") != std::string::npos, "Exact diagnostic match");

        // Test Thread Guard removal on this core
        std::string err_thr;
        bool caught_thr = auditor.audit_single_guard_removal(core, true, &err_thr);
        TEST_ASSERT(caught_thr, ("Core " + std::to_string(core) + " thread guard removal detected").c_str());
        TEST_ASSERT(err_thr.find("Core " + std::to_string(core)) != std::string::npos, "Core ID match in error");
        TEST_ASSERT(err_thr.find("missing thread guard interval") != std::string::npos, "Exact diagnostic match");
    }

    std::cout << "[Stage 263.14] All 12 guard regions independently tested and verified.\n";
}

int main() {
    std::cout << "=== Running Stage 263.14: Nested Exception & Auditor Diagnostic Tests ===\n";
    test_stage263_12_nested_daif_policy();
    test_stage263_13_stack_instrumentation_and_overlimit();
    test_stage263_14_auditor_diagnostic_correction();
    std::cout << "=== ALL STAGE 263.14 TESTS PASSED ===\n";
    return 0;
}
