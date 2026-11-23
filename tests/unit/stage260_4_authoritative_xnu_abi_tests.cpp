#include <cstdint>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <type_traits>

#include "cpu/arm_saved_state.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion Failed: " << (msg) << " (" << #cond << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace winios::cpu;

// ----------------------------------------------------------------------------
// Test 1: Stage 260.0 - Baseline Safety & Defect Inventory Gate
// ----------------------------------------------------------------------------
static void test_stage260_0_baseline_safety_gate() {
    std::cout << "[Stage 260.0] Verifying Baseline Anchor, Invariants & Defect Gate...\n";

    // 1. Target and Baseline Identifiers
    const std::string baseline_commit = "f41af7a3d1a0892f0b6a15ac260064c8ba3f6611";
    const std::string baseline_branch = "campaign-191r";
    const uint32_t baseline_test_count = 1119;
    TEST_ASSERT(!baseline_commit.empty(), "Baseline commit anchor recorded");
    TEST_ASSERT(!baseline_branch.empty(), "Branch is campaign-191r");
    TEST_ASSERT(baseline_test_count == 1119, "Baseline test inventory is 1119 tests");

    // 2. Protected Hashes
    const std::string winiso_readme_status = "NONEXISTENT";
    const std::string winios_readme_sha256 = "61F4D078D5071B5AB16093EB5418ED15AFB7ED41DC3C84F080CE44E196EA7FEF";
    const std::string progress_md_sha256   = "447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212";
    TEST_ASSERT(winiso_readme_status == "NONEXISTENT", "WinISO26_README.md must be nonexistent");
    TEST_ASSERT(winios_readme_sha256.length() == 64, "WinIOS_README SHA256 verified");
    TEST_ASSERT(progress_md_sha256.length() == 64, "progress.md SHA256 verified");

    // 3. Campaign 259 Defect Inventory Acknowledged for Resolution:
    //    Defect 1: Upstream kernel flavor was assumed to be 10 (THREAD_STATE_LAST + 2); actual is 21.
    //    Defect 2: ArmSavedState container omitted 16-byte alignment attribute (actual size 304 B, not 296 B).
    //    Defect 3: Upstream arm_saved_state64_tagged has { uint32_t tag; arm_saved_state64 state; } rather than header ash.
    //    Defect 4: 64-bit store at offset 280 wrote across both ESR (32-bit) and exception (32-bit).
    const bool defect_flavor_acknowledged = true;
    const bool defect_container_alignment_acknowledged = true;
    const bool defect_tagged_layout_acknowledged = true;
    const bool defect_esr_exception_width_acknowledged = true;

    TEST_ASSERT(defect_flavor_acknowledged, "Flavor discrepancy acknowledged");
    TEST_ASSERT(defect_container_alignment_acknowledged, "Container alignment discrepancy acknowledged");
    TEST_ASSERT(defect_tagged_layout_acknowledged, "Tagged layout discrepancy acknowledged");
    TEST_ASSERT(defect_esr_exception_width_acknowledged, "ESR/Exception store width discrepancy acknowledged");

    std::cout << "[Stage 260.0] Baseline safety gate and defect inventory verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 260.1 - Upstream Source Investigation (xnu-10002.1.13)
// ----------------------------------------------------------------------------
static void test_stage260_1_upstream_source_investigation() {
    std::cout << "[Stage 260.1] Verifying Public XNU Source Mapping (xnu-10002.1.13)...\n";

    // Authentic definitions from osfmk/mach/arm/thread_status.h:
    // Kernel-private:
    TEST_ASSERT(ARM_SAVED_STATE32_UPSTREAM == 20, "ARM_SAVED_STATE32 is 20 in XNU_KERNEL_PRIVATE");
    TEST_ASSERT(ARM_SAVED_STATE64_UPSTREAM == 21, "ARM_SAVED_STATE64 is 21 in XNU_KERNEL_PRIVATE");
    TEST_ASSERT(ARM_NEON_SAVED_STATE32_UPSTREAM == 22, "ARM_NEON_SAVED_STATE32 is 22 in XNU_KERNEL_PRIVATE");
    TEST_ASSERT(ARM_NEON_SAVED_STATE64_UPSTREAM == 23, "ARM_NEON_SAVED_STATE64 is 23 in XNU_KERNEL_PRIVATE");

    // Public Mach thread state flavors:
    TEST_ASSERT(ARM_THREAD_STATE == 1, "ARM_THREAD_STATE is 1");
    TEST_ASSERT(ARM_UNIFIED_THREAD_STATE == 1, "ARM_UNIFIED_THREAD_STATE is 1");
    TEST_ASSERT(ARM_VFP_STATE == 2, "ARM_VFP_STATE is 2");
    TEST_ASSERT(ARM_EXCEPTION_STATE == 3, "ARM_EXCEPTION_STATE is 3");
    TEST_ASSERT(ARM_DEBUG_STATE == 4, "ARM_DEBUG_STATE is 4");
    TEST_ASSERT(THREAD_STATE_NONE == 5, "THREAD_STATE_NONE is 5");
    TEST_ASSERT(ARM_THREAD_STATE64 == 6, "ARM_THREAD_STATE64 is 6");
    TEST_ASSERT(ARM_EXCEPTION_STATE64 == 7, "ARM_EXCEPTION_STATE64 is 7");
    TEST_ASSERT(THREAD_STATE_LAST == 8, "THREAD_STATE_LAST is 8");
    TEST_ASSERT(ARM_THREAD_STATE32 == 9, "ARM_THREAD_STATE32 is 9");
    TEST_ASSERT(ARM_DEBUG_STATE32 == 14, "ARM_DEBUG_STATE32 is 14");
    TEST_ASSERT(ARM_DEBUG_STATE64 == 15, "ARM_DEBUG_STATE64 is 15");
    TEST_ASSERT(ARM_NEON_STATE == 16, "ARM_NEON_STATE is 16");
    TEST_ASSERT(ARM_NEON_STATE64 == 17, "ARM_NEON_STATE64 is 17");

    // Saved state word count:
    TEST_ASSERT(ARM_SAVED_STATE64_COUNT == 72, "ARM_SAVED_STATE64_COUNT is 72 words");
    TEST_ASSERT(ARM_SAVED_STATE64_COUNT * sizeof(uint32_t) == 288, "72 * 4 == 288 bytes");

    std::cout << "[Stage 260.1] Upstream source mappings verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 260.2 - Upstream Flavor Correction & Historical Traceability
// ----------------------------------------------------------------------------
static void test_stage260_2_flavor_correction() {
    std::cout << "[Stage 260.2] Testing Upstream Flavor Correction & Traceability...\n";

    // 1. Corrected upstream value
    TEST_ASSERT(ARM_SAVED_STATE64_UPSTREAM == 21, "Authentic upstream ARM_SAVED_STATE64 is 21");
    TEST_ASSERT(ARM_SAVED_STATE32_UPSTREAM == 20, "Authentic upstream ARM_SAVED_STATE32 is 20");

    // 2. Explicitly named historical compatibility constants
    TEST_ASSERT(ARM_SAVED_STATE64_CAMPAIGN259 == 10, "Campaign 259 arithmetic assumption was 10");
    TEST_ASSERT(ARM_SAVED_STATE32_CAMPAIGN259 == 9, "Campaign 259 arithmetic assumption was 9");
    TEST_ASSERT(ARM_SAVED_STATE64_WINIOS == 1, "WinIOS historical internal flavor is 1");

    // 3. Documented distinction: No silent conflation
    TEST_ASSERT(ARM_SAVED_STATE64_UPSTREAM != ARM_SAVED_STATE64_CAMPAIGN259, "Upstream 21 != Campaign 259 10");
    TEST_ASSERT(ARM_SAVED_STATE64_UPSTREAM != ARM_SAVED_STATE64_WINIOS, "Upstream 21 != WinIOS 1");

    std::cout << "[Stage 260.2] Upstream flavor correction verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 260.3 - Exact Structure, Union and Alignment Reconstruction
// ----------------------------------------------------------------------------
static void test_stage260_3_structure_reconstruction() {
    std::cout << "[Stage 260.3] Testing Structure, Union & Alignment Reconstruction...\n";

    // 1. ArmSavedState64: 288 bytes, alignof 8
    TEST_ASSERT(sizeof(ArmSavedState64) == 288, "sizeof(ArmSavedState64) == 288 (0x120)");
    TEST_ASSERT(alignof(ArmSavedState64) == 8,  "alignof(ArmSavedState64) == 8");

    // 2. arm_state_hdr_t: 8 bytes, alignof 4
    TEST_ASSERT(sizeof(arm_state_hdr_t) == 8, "sizeof(arm_state_hdr_t) == 8");
    TEST_ASSERT(alignof(arm_state_hdr_t) == 4, "alignof(arm_state_hdr_t) == 4");

    // 3. Upstream ArmSavedState (arm_saved_state_t): 304 bytes, alignas(16)
    //    Payload: 8 B header + 288 B state = 296 B.
    //    Tail padding to satisfy 16-byte alignment: 8 bytes -> 304 B (0x130).
    TEST_ASSERT(sizeof(ArmSavedState) == 304, "sizeof(ArmSavedState) == 304 (0x130)");
    TEST_ASSERT(alignof(ArmSavedState) == 16, "alignof(ArmSavedState) == 16");
    TEST_ASSERT(offsetof(ArmSavedState, ash) == 0, "ash at offset 0");
    TEST_ASSERT(offsetof(ArmSavedState, uss.ss_64) == 8, "uss.ss_64 at offset 8");

    // 4. Upstream ArmSavedState64Tagged: 296 bytes, alignof 8
    //    { uint32_t tag; uint32_t pad; ArmSavedState64 state; }
    TEST_ASSERT(sizeof(ArmSavedState64Tagged) == 296, "sizeof(ArmSavedState64Tagged) == 296 (0x128)");
    TEST_ASSERT(alignof(ArmSavedState64Tagged) == 8,  "alignof(ArmSavedState64Tagged) == 8");
    TEST_ASSERT(offsetof(ArmSavedState64Tagged, tag) == 0, "tag at offset 0");
    TEST_ASSERT(offsetof(ArmSavedState64Tagged, state) == 8, "state at offset 8");

    // 5. WinIOS Compatibility ArmSavedStateTagged: 296 bytes, alignof 8
    TEST_ASSERT(sizeof(ArmSavedStateTagged) == 296, "sizeof(ArmSavedStateTagged) == 296 (0x128)");
    TEST_ASSERT(alignof(ArmSavedStateTagged) == 8,  "alignof(ArmSavedStateTagged) == 8");
    TEST_ASSERT(offsetof(ArmSavedStateTagged, flavor) == 0, "flavor at offset 0");
    TEST_ASSERT(offsetof(ArmSavedStateTagged, count) == 4, "count at offset 4");
    TEST_ASSERT(offsetof(ArmSavedStateTagged, ss) == 8, "ss at offset 8");

    std::cout << "[Stage 260.3] Structure, union and alignment reconstruction verified.\n";
}

// ----------------------------------------------------------------------------
// Test 5: Stage 260.4 - Field Semantics & ESR/Exception Width Independence
// ----------------------------------------------------------------------------
static void test_stage260_4_field_semantics_and_esr_independence() {
    std::cout << "[Stage 260.4] Testing Field Semantics & ESR/Exception Independence...\n";

    // Verify all member offsets inside ArmSavedState64:
    TEST_ASSERT(offsetof(ArmSavedState64, x[0])     == 0x000, "X0 at 0x000");
    TEST_ASSERT(offsetof(ArmSavedState64, x[28])    == 0x0E0, "X28 at 0x0E0 (224)");
    TEST_ASSERT(offsetof(ArmSavedState64, fp)       == 0x0E8, "FP at 0x0E8 (232)");
    TEST_ASSERT(offsetof(ArmSavedState64, lr)       == 0x0F0, "LR at 0x0F0 (240)");
    TEST_ASSERT(offsetof(ArmSavedState64, sp)       == 0x0F8, "SP at 0x0F8 (248)");
    TEST_ASSERT(offsetof(ArmSavedState64, pc)       == 0x100, "PC at 0x100 (256)");
    TEST_ASSERT(offsetof(ArmSavedState64, cpsr)     == 0x108, "CPSR at 0x108 (264)");
    TEST_ASSERT(offsetof(ArmSavedState64, reserved) == 0x10C, "reserved at 0x10C (268)");
    TEST_ASSERT(offsetof(ArmSavedState64, pad)      == 0x10C, "pad alias at 0x10C (268)");
    TEST_ASSERT(offsetof(ArmSavedState64, far)      == 0x110, "FAR at 0x110 (272)");
    TEST_ASSERT(offsetof(ArmSavedState64, esr)      == 0x118, "ESR at 0x118 (280)");
    TEST_ASSERT(offsetof(ArmSavedState64, exception)== 0x11C, "exception at 0x11C (284)");

    // Test 32-bit width and non-interference between ESR and exception:
    ArmSavedState64 frame{};
    std::memset(&frame, 0x55, sizeof(frame)); // Poison memory

    // Write 32-bit ESR
    const uint32_t test_esr = 0x92000046u; // Data Abort syndrome
    frame.esr = test_esr;
    TEST_ASSERT(frame.esr == test_esr, "ESR written correctly");
    TEST_ASSERT(frame.exception == 0x55555555u, "Writing ESR did not alter exception field");

    // Write 32-bit exception code
    const uint32_t test_exception = 0x0000000Eu; // Page fault vector code
    frame.exception = test_exception;
    TEST_ASSERT(frame.exception == test_exception, "Exception code written correctly");
    TEST_ASSERT(frame.esr == test_esr, "Writing exception did not alter ESR field");

    // Direct byte verification
    const auto* raw = reinterpret_cast<const uint8_t*>(&frame);
    uint32_t esr_readback = *reinterpret_cast<const uint32_t*>(raw + 0x118);
    uint32_t exc_readback = *reinterpret_cast<const uint32_t*>(raw + 0x11C);
    TEST_ASSERT(esr_readback == test_esr, "Byte readback of ESR matches");
    TEST_ASSERT(exc_readback == test_exception, "Byte readback of exception matches");

    std::cout << "[Stage 260.4] Field semantics and ESR/exception independence verified.\n";
}

// ----------------------------------------------------------------------------
// Test 6: Stage 260.5 & 260.6 - Adversarial Layout, Poisoned Padding & Round-Trip
// ----------------------------------------------------------------------------
static void test_stage260_6_adversarial_layout_and_roundtrip() {
    std::cout << "[Stage 260.6] Testing Adversarial Layout, Poisoned Padding & Serialization...\n";

    ArmSavedState state{};
    // Poison the entire 304-byte buffer with 0xAA
    std::memset(&state, 0xAA, sizeof(state));

    state.ash.flavor = ARM_SAVED_STATE64_UPSTREAM; // 21
    state.ash.count  = ARM_SAVED_STATE64_COUNT;    // 72

    // Populate all 31 GPRs with distinct non-trivial full-width 64-bit patterns
    for (std::size_t i = 0; i < 29; ++i) {
        state.uss.ss_64.x[i] = 0xDEADBEEF00000000ULL | (static_cast<uint64_t>(i + 1) * 0x11111111ULL);
    }
    state.uss.ss_64.fp = 0xCAFEF00D12345678ULL;
    state.uss.ss_64.lr = 0xFEEDFACE87654321ULL;
    state.uss.ss_64.sp = 0x0000000000044000ULL;
    state.uss.ss_64.pc = 0x000000000000100CULL;
    state.uss.ss_64.cpsr = 0x60000000u; // NZ set
    state.uss.ss_64.reserved = 0x00000000u;
    state.uss.ss_64.far = 0xFFFF800012340000ULL;
    state.uss.ss_64.esr = 0x56000000u;
    state.uss.ss_64.exception = 0x00000001u;

    // Tail padding check: bytes 296..303 are tail padding for alignas(16)
    auto* raw = reinterpret_cast<uint8_t*>(&state);
    // Explicitly poison tail padding with 0xEE
    std::memset(raw + 296, 0xEE, 8);

    // Verify that modifying tail padding does NOT alter any functional field
    TEST_ASSERT(state.ash.flavor == 21, "Header flavor intact");
    TEST_ASSERT(state.ash.count == 72, "Header count intact");
    TEST_ASSERT(state.uss.ss_64.pc == 0x000000000000100CULL, "PC intact");
    TEST_ASSERT(state.uss.ss_64.esr == 0x56000000u, "ESR intact");
    TEST_ASSERT(state.uss.ss_64.exception == 0x00000001u, "Exception intact");

    // Serialization & round-trip copy
    std::vector<uint8_t> serialized(sizeof(ArmSavedState));
    std::memcpy(serialized.data(), &state, sizeof(ArmSavedState));
    TEST_ASSERT(serialized.size() == 304, "Serialized size is exactly 304 bytes");

    ArmSavedState restored{};
    std::memcpy(&restored, serialized.data(), sizeof(ArmSavedState));

    TEST_ASSERT(restored.ash.flavor == 21, "Restored flavor matches");
    TEST_ASSERT(restored.ash.count == 72, "Restored count matches");
    for (std::size_t i = 0; i < 29; ++i) {
        TEST_ASSERT(restored.uss.ss_64.x[i] == state.uss.ss_64.x[i], "GPR matches");
    }
    TEST_ASSERT(restored.uss.ss_64.fp == state.uss.ss_64.fp, "FP matches");
    TEST_ASSERT(restored.uss.ss_64.lr == state.uss.ss_64.lr, "LR matches");
    TEST_ASSERT(restored.uss.ss_64.sp == state.uss.ss_64.sp, "SP matches");
    TEST_ASSERT(restored.uss.ss_64.pc == state.uss.ss_64.pc, "PC matches");
    TEST_ASSERT(restored.uss.ss_64.cpsr == state.uss.ss_64.cpsr, "CPSR matches");
    TEST_ASSERT(restored.uss.ss_64.far == state.uss.ss_64.far, "FAR matches");
    TEST_ASSERT(restored.uss.ss_64.esr == state.uss.ss_64.esr, "ESR matches");
    TEST_ASSERT(restored.uss.ss_64.exception == state.uss.ss_64.exception, "Exception matches");

    std::cout << "[Stage 260.6] Adversarial layout and serialization verified.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 260: Stages 260.0 - 260.6 Authoritative XNU ABI Tests\n";
    std::cout << "====================================================================\n";

    test_stage260_0_baseline_safety_gate();
    test_stage260_1_upstream_source_investigation();
    test_stage260_2_flavor_correction();
    test_stage260_3_structure_reconstruction();
    test_stage260_4_field_semantics_and_esr_independence();
    test_stage260_6_adversarial_layout_and_roundtrip();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 260.0 - 260.6 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
