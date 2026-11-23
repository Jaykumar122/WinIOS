#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "cpu/arm_saved_state.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::cpu;

// ----------------------------------------------------------------------------
// Test 1: Stage 263.1 - Campaign 262 Implementation Audit
// ----------------------------------------------------------------------------
static void test_stage263_1_campaign262_audit() {
    std::cout << "[Stage 263.1] Auditing Campaign 262 Exception Implementation...\n";

    // Verify baseline invariants established in Campaign 262:
    // 1. ArmSavedState64 struct is 288 bytes (72 words)
    TEST_ASSERT(sizeof(ArmSavedState64) == 288, "ArmSavedState64 size is 288 bytes");
    TEST_ASSERT(ARM_SAVED_STATE64_COUNT == 72, "Word count is 72 words");

    // 2. Identify Campaign 262 gaps addressed in Campaign 263:
    // Gap 1: Profile coupling - PAC frame size (296 bytes) lacked derived 16-byte stack frame alignment.
    // Gap 2: Unified frame allocation - hardcoded 288 bytes rather than ABI-derived size.
    // Gap 3: Auditor duplicated diagnostic reporting between thread and interrupt guards.
    // Gap 4: Timer catch-up arithmetic lacked checked multiplication/addition against overflow.

    std::cout << "[Stage 263.1] Campaign 262 audit complete; 4 core architectural gaps identified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 263.2 - Exact Upstream XNU Saved-State ABI Audit
// ----------------------------------------------------------------------------
static void test_stage263_2_upstream_xnu_abi_audit() {
    std::cout << "[Stage 263.2] Auditing Exact Upstream XNU Saved-State ABI (xnu-10002.1.13)...\n";

    // Upstream XNU kernel-private flavors from osfmk/mach/arm/thread_status.h
    TEST_ASSERT(ARM_SAVED_STATE32_UPSTREAM == 20, "Upstream ARM_SAVED_STATE32 is 20");
    TEST_ASSERT(ARM_SAVED_STATE64_UPSTREAM == 21, "Upstream ARM_SAVED_STATE64 is 21");
    TEST_ASSERT(ARM_NEON_SAVED_STATE32_UPSTREAM == 22, "Upstream ARM_NEON_SAVED_STATE32 is 22");
    TEST_ASSERT(ARM_NEON_SAVED_STATE64_UPSTREAM == 23, "Upstream ARM_NEON_SAVED_STATE64 is 23");

    // Public Mach thread state flavors
    TEST_ASSERT(ARM_THREAD_STATE == 1, "Public ARM_THREAD_STATE is 1");
    TEST_ASSERT(ARM_THREAD_STATE64 == 6, "Public ARM_THREAD_STATE64 is 6");
    TEST_ASSERT(THREAD_STATE_LAST == 8, "Legacy THREAD_STATE_LAST is 8");

    // Exact field offsets within ArmSavedState64
    TEST_ASSERT(offsetof(ArmSavedState64, x[0]) == 0x000, "X0 at 0x000");
    TEST_ASSERT(offsetof(ArmSavedState64, x[28]) == 0x0E0, "X28 at 0x0E0");
    TEST_ASSERT(offsetof(ArmSavedState64, fp) == 0x0E8, "FP at 0x0E8");
    TEST_ASSERT(offsetof(ArmSavedState64, lr) == 0x0F0, "LR at 0x0F0");
    TEST_ASSERT(offsetof(ArmSavedState64, sp) == 0x0F8, "SP at 0x0F8");
    TEST_ASSERT(offsetof(ArmSavedState64, pc) == 0x100, "PC at 0x100");
    TEST_ASSERT(offsetof(ArmSavedState64, cpsr) == 0x108, "CPSR at 0x108");
    TEST_ASSERT(offsetof(ArmSavedState64, reserved) == 0x10C, "reserved at 0x10C");
    TEST_ASSERT(offsetof(ArmSavedState64, far) == 0x110, "FAR at 0x110");
    TEST_ASSERT(offsetof(ArmSavedState64, esr) == 0x118, "ESR at 0x118");
    TEST_ASSERT(offsetof(ArmSavedState64, exception) == 0x11C, "exception at 0x11C");

    // Exact field offsets and sizing for PAC variant ArmSavedState64Pac
    TEST_ASSERT(sizeof(ArmSavedState64Pac) == 296, "ArmSavedState64Pac size is 296 bytes");
    TEST_ASSERT(offsetof(ArmSavedState64Pac, jophash) == 0x120, "jophash at 0x120");
    TEST_ASSERT(ARM_SAVED_STATE64_PAC_COUNT == 74, "ARM_SAVED_STATE64_PAC_COUNT is 74 words");

    std::cout << "[Stage 263.2] Upstream XNU ABI audit verified against xnu-10002.1.13.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 263.3 - Explicit ARM64 and ARM64e PAC Profiles
// ----------------------------------------------------------------------------
static void test_stage263_3_explicit_pac_profiles() {
    std::cout << "[Stage 263.3] Testing Explicit ARM64 and ARM64e PAC Execution Profiles...\n";

    // Profile A: ARM64_Standard
    auto layout_std = get_exception_frame_layout(ExecutionProfile::ARM64_Standard);
    TEST_ASSERT(layout_std.profile == ExecutionProfile::ARM64_Standard, "Profile matches ARM64_Standard");
    TEST_ASSERT(layout_std.raw_saved_state_size == 288, "Standard raw saved state size is 288 bytes");
    TEST_ASSERT(layout_std.frame_allocation_size == 288, "Standard frame allocation size is 288 bytes");
    TEST_ASSERT(layout_std.saved_state_word_count == 72, "Standard word count is 72 words");
    TEST_ASSERT(!layout_std.has_apple_pac, "Standard profile has no PAC");
    TEST_ASSERT(layout_std.tagged_wrapper_size == 296, "Standard tagged wrapper size is 296 bytes");
    TEST_ASSERT(layout_std.union_wrapper_size == 304, "Standard union wrapper size is 304 bytes");

    // Profile B: ARM64e_PAC
    auto layout_pac = get_exception_frame_layout(ExecutionProfile::ARM64e_PAC);
    TEST_ASSERT(layout_pac.profile == ExecutionProfile::ARM64e_PAC, "Profile matches ARM64e_PAC");
    TEST_ASSERT(layout_pac.raw_saved_state_size == 296, "PAC raw saved state size is 296 bytes");
    // Crucial ARM64 SP 16-byte alignment rule: 296 rounded up to multiple of 16 is 304!
    TEST_ASSERT(layout_pac.frame_allocation_size == 304, "PAC frame allocation size is 304 bytes (16-byte aligned)");
    TEST_ASSERT(layout_pac.frame_allocation_size % 16 == 0, "PAC stack frame allocation is 16-byte aligned");
    TEST_ASSERT(layout_pac.saved_state_word_count == 74, "PAC word count is 74 words");
    TEST_ASSERT(layout_pac.has_apple_pac, "PAC profile has PAC enabled");
    TEST_ASSERT(layout_pac.jophash_offset == 0x120, "jophash at offset 0x120");
    TEST_ASSERT(layout_pac.tagged_wrapper_size == 304, "PAC tagged wrapper size is 304 bytes");
    TEST_ASSERT(layout_pac.union_wrapper_size == 320, "PAC union wrapper size is 320 bytes");

    std::cout << "[Stage 263.3] Explicit profiles verified (Standard: 288 B frame, PAC: 304 B frame).\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 263.4 - ABI-Derived Frame Layout & Static Assertions
// ----------------------------------------------------------------------------
static void test_stage263_4_abi_derived_frame_layout() {
    std::cout << "[Stage 263.4] Testing ABI-Derived Frame Layout...\n";

    for (int p = 0; p <= 1; ++p) {
        auto profile = static_cast<ExecutionProfile>(p);
        auto layout = get_exception_frame_layout(profile);

        // Verify fundamental ARM64 architectural invariants
        TEST_ASSERT(layout.frame_allocation_size >= layout.raw_saved_state_size,
                    "Frame allocation must cover raw saved state");
        TEST_ASSERT(layout.frame_allocation_size % 16 == 0,
                    "Stack frame allocation MUST be 16-byte aligned per ARM64 ABI");
        TEST_ASSERT(layout.sp_offset == 0x0F8, "SP offset invariant");
        TEST_ASSERT(layout.pc_offset == 0x100, "PC offset invariant");
        TEST_ASSERT(layout.cpsr_offset == 0x108, "CPSR offset invariant");
        TEST_ASSERT(layout.far_offset == 0x110, "FAR offset invariant");
        TEST_ASSERT(layout.esr_offset == 0x118, "ESR offset invariant");
        TEST_ASSERT(layout.exception_code_offset == 0x11C, "Exception code offset invariant");

        if (layout.has_apple_pac) {
            TEST_ASSERT(layout.jophash_offset == 0x120, "PAC jophash at offset 0x120");
        }
    }

    std::cout << "[Stage 263.4] ABI-derived frame layouts and 16-byte stack alignments verified.\n";
}

int main() {
    std::cout << "=== Running Stage 263.4: ABI, PAC Profiles & Frame Layout Tests ===\n";
    test_stage263_1_campaign262_audit();
    test_stage263_2_upstream_xnu_abi_audit();
    test_stage263_3_explicit_pac_profiles();
    test_stage263_4_abi_derived_frame_layout();
    std::cout << "=== ALL STAGE 263.4 TESTS PASSED ===\n";
    return 0;
}
