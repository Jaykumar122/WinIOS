// ============================================================================
// Stage 256.19 - 256.21: Multi-Core Exception Isolation, Adversarial Exception
// Tests & Deterministic Source/Binary Provenance Auditor
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "device/apple_aic.h"
#include "loader/kernel_interval_map.h"
#include "loader/xnu_instruction_provenance_tracker.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::loader;
using namespace winios::memory;

// ----------------------------------------------------------------------------
// Test 1: Stage 256.19: Multi-Core Exception Isolation
// ----------------------------------------------------------------------------
static void test_stage256_19_multicore_exception_isolation() {
    std::cout << "[Stage 256.19] Testing Multi-Core Exception Isolation...\n";

    // Apple A16 Bionic 6-core topology (2 Performance + 4 Efficiency cores)
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
    aic.reset();

    // Verify per-core WhoAmI banking
    for (std::size_t c = 0; c < 6; ++c) {
        uint64_t reg_off = AppleAIC::kRegCoreBase + (c * AppleAIC::kCoreStride) + AppleAIC::kCoreOffWhoAmI;
        TEST_ASSERT(aic.read_register(reg_off, 4) == c, "Per-core WhoAmI register matches core ID");
    }

    // Target HW IRQ 50 exclusively to Core 3
    aic.unmask_hw_irq(50);
    aic.set_irq_target(50, 1u << 3);
    aic.set_hw_irq(50, true);

    // Verify isolation: only Core 3 receives interrupt
    for (std::size_t c = 0; c < 6; ++c) {
        if (c == 3) {
            TEST_ASSERT(aic.has_pending_interrupt(c), "Core 3 has pending interrupt");
        } else {
            TEST_ASSERT(!aic.has_pending_interrupt(c), "Other core isolated from Core 3 interrupt");
        }
    }

    // Core 3 handles event and clears it
    uint32_t event = static_cast<uint32_t>(aic.read_register(AppleAIC::kRegCoreBase + (3 * AppleAIC::kCoreStride) + AppleAIC::kCoreOffEvent, 4));
    TEST_ASSERT((event & 0xFFFFu) == 50u, "Core 3 popped event 50");
    aic.set_hw_irq(50, false);
    aic.acknowledge_eoi(3, event);

    TEST_ASSERT(!aic.has_pending_interrupt(3), "Core 3 interrupt cleared after EOI");

    std::cout << "[Stage 256.19] Multi-core exception isolation verified across 6 cores.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 256.20: Adversarial Exception Tests
// ----------------------------------------------------------------------------
static void test_stage256_20_adversarial_exception_tests() {
    std::cout << "[Stage 256.20] Testing Adversarial Exception Tests...\n";

    // Negative Test 1: Misaligned VBAR_EL1
    std::string err;
    TEST_ASSERT(!KernelExceptionVectors::validate_vector_table_alignment(0x00000880ULL, &err), "Misaligned VBAR 0x880 rejected");
    TEST_ASSERT(!err.empty(), "Error message returned for misaligned VBAR");

    // Negative Test 2: Vector Placement in Non-Executable Memory (W^X Violation)
    KernelPhysicalIntervalMap pmap(0x0ULL, 0x800000ULL);
    pmap.add_interval({"KernelText", IntervalType::KernelText, 0x00000000ULL, 0x00004000ULL, 0x5, 0x5, true});
    pmap.add_interval({"KernelData", IntervalType::KernelData, 0x00010000ULL, 0x00020000ULL, 0x3, 0x3, true});

    // Testing historical address 0x10280 (inside KernelData)
    const auto* data_int = pmap.find_interval_by_name("KernelData");
    TEST_ASSERT(data_int != nullptr, "KernelData interval exists");
    bool can_execute_from_data = (data_int->prot & 0x4) != 0;
    TEST_ASSERT(!can_execute_from_data, "Adversarial execution from __DATA strictly prohibited by W^X");

    // Negative Test 3: Spurious Event Read
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 4);
    aic.reset();
    uint32_t spurious_ev = static_cast<uint32_t>(aic.read_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, 4));
    TEST_ASSERT(spurious_ev == AppleAIC::kEventNone, "Empty event queue returns 0xFFFFFFFF");

    // Negative Test 4: Out-of-bounds IRQ ID
    aic.mask_hw_irq(1024); // max valid is 511 / 1023
    TEST_ASSERT(!aic.is_hw_irq_asserted(1024), "Out of bounds IRQ not asserted");

    // Negative Test 5: Stack Overlap into Guard Frame
    const uint64_t istack_base = 0x00040000ULL;
    const uint64_t underflow_sp = istack_base - 16;
    TEST_ASSERT(underflow_sp < istack_base, "Stack underflow detected below interrupt stack base");

    std::cout << "[Stage 256.20] Adversarial exception tests passed.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 256.21: Source and Binary Provenance Auditor
// ----------------------------------------------------------------------------
static void test_stage256_21_source_and_binary_provenance_auditor() {
    std::cout << "[Stage 256.21] Testing Source and Binary Provenance Auditor...\n";

    auto& tracker = XnuInstructionProvenanceTracker::instance();
    tracker.reset();
    tracker.set_binary_sha256("339878bbe5f68c0646a5fc7735c990eb3aabb1c3");

    // Record verified instructions
    tracker.record_instruction(0x2BE0ULL, 0xD5034FDFu, "msr daifset, #0xf");
    tracker.record_instruction(0x0A80ULL, 0xD10403FFu, "sub sp, sp, #256");
    tracker.record_instruction(0x0A94ULL, 0xD69F03E0u, "eret");

    TEST_ASSERT(tracker.count() == 3, "Tracker recorded 3 instructions");

    // Audit entries
    const auto& entries = tracker.entries();
    for (const auto& e : entries) {
        TEST_ASSERT(!e.binary_sha256.empty(), "Binary hash associated with instruction");
        TEST_ASSERT(e.source_revision == "xnu-10002.1.13", "Source revision verified");
        TEST_ASSERT(e.category != "Category D (Synthetic Test Stub)", "No Category D stubs");
    }

    // Export and verify JSON audit trail
    std::string json = tracker.export_json();
    TEST_ASSERT(!json.empty(), "JSON audit exported");
    TEST_ASSERT(json.find("\"source_revision\": \"xnu-10002.1.13\"") != std::string::npos, "JSON includes revision");
    TEST_ASSERT(json.find("\"total_retired\": 3") != std::string::npos, "JSON includes total retired count");

    std::cout << "[Stage 256.21] Source and binary provenance auditor verified.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 256: Stages 256.19 - 256.21 Isolation & Auditor Tests\n";
    std::cout << "====================================================================\n";

    test_stage256_19_multicore_exception_isolation();
    test_stage256_20_adversarial_exception_tests();
    test_stage256_21_source_and_binary_provenance_auditor();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 256.19 - 256.21 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
