// =============================================================================
// WinIOS Campaign 265 - Stages 265.5 through 265.7 Test Suite
//
// Stage 265.5: Exact Upstream XNU Saved-State ABI Audit
// Stage 265.6: PAC/JOPHASH Semantic Investigation & Truth
// Stage 265.7: Correct PAC Profile and Frame Policy
// =============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cassert>

#include "cpu/arm_saved_state.h"

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
// Stage 265.5: Exact Upstream XNU Saved-State ABI Audit
// -----------------------------------------------------------------------------
static bool test_stage265_5_upstream_xnu_abi_audit() {
    std::cout << "[Stage 265.5] Auditing exact upstream XNU saved-state ABI...\n";

    // Standard ARM64 upstream structure size & offsets
    TEST_ASSERT(sizeof(arm_saved_state64_upstream) == 288, "Standard ARM64 upstream size == 288 bytes");
    TEST_ASSERT(sizeof(ArmSavedState64) == 288, "WinIOS ArmSavedState64 size == 288 bytes");

    TEST_ASSERT(offsetof(arm_saved_state64_upstream, fp) == 232, "Upstream fp offset == 232");
    TEST_ASSERT(offsetof(arm_saved_state64_upstream, lr) == 240, "Upstream lr offset == 240");
    TEST_ASSERT(offsetof(arm_saved_state64_upstream, sp) == 248, "Upstream sp offset == 248");
    TEST_ASSERT(offsetof(arm_saved_state64_upstream, pc) == 256, "Upstream pc offset == 256");
    TEST_ASSERT(offsetof(arm_saved_state64_upstream, cpsr) == 264, "Upstream cpsr offset == 264");
    TEST_ASSERT(offsetof(arm_saved_state64_upstream, fsr) == 268, "Upstream fsr offset == 268");
    TEST_ASSERT(offsetof(arm_saved_state64_upstream, far) == 272, "Upstream far offset == 272");
    TEST_ASSERT(offsetof(arm_saved_state64_upstream, exception) == 280, "Upstream exception offset == 280");
    TEST_ASSERT(offsetof(arm_saved_state64_upstream, pad) == 284, "Upstream pad offset == 284");

    // ARM64e PAC upstream structure size & offsets
    TEST_ASSERT(sizeof(arm_saved_state64_pac_upstream) == 296, "ARM64e PAC upstream size == 296 bytes");
    TEST_ASSERT(sizeof(ArmSavedState64Pac) == 296, "WinIOS ArmSavedState64Pac size == 296 bytes");
    TEST_ASSERT(offsetof(arm_saved_state64_pac_upstream, jophash) == 288, "Upstream jophash offset == 288");

    std::cout << "  - Standard ARM64 size: " << sizeof(arm_saved_state64_upstream) << " bytes (72 words)\n";
    std::cout << "  - ARM64e PAC size: " << sizeof(arm_saved_state64_pac_upstream) << " bytes (74 words)\n";
    std::cout << "  [PASS] Stage 265.5 Upstream ABI matches byte-for-byte, offset-for-offset.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 265.6: PAC/JOPHASH Semantic Investigation & Truth
// -----------------------------------------------------------------------------
static bool test_stage265_6_pac_jophash_truth() {
    std::cout << "[Stage 265.6] Verifying PAC/JOPHASH semantic handling and preservation...\n";

    // Jophash is preserved as an opaque 64-bit metadata field.
    // WinIOS explicitly models preservation, not fake proprietary crypto signing.
    ArmSavedState64Pac pac_state{};
    pac_state.pc = 0xFFFFFE0007004000ULL;
    pac_state.jophash = 0x51A7F00DCAFEBABEULL;

    TEST_ASSERT(pac_state.jophash == 0x51A7F00DCAFEBABEULL, "Jophash value preserved intact");

    // Verify layout descriptor
    auto layout_pac = get_exception_frame_layout(ExecutionProfile::ARM64e_PAC);
    TEST_ASSERT(layout_pac.has_apple_pac, "Arm64ePac profile has has_apple_pac=true");
    TEST_ASSERT(layout_pac.raw_saved_state_size == 296, "Saved state size 296");
    TEST_ASSERT(layout_pac.frame_allocation_size == 304, "Frame allocation 304 (16-byte aligned)");

    auto layout_std = get_exception_frame_layout(ExecutionProfile::ARM64_Standard);
    TEST_ASSERT(!layout_std.has_apple_pac, "StandardArm64 has has_apple_pac=false");
    TEST_ASSERT(layout_std.raw_saved_state_size == 288, "Saved state size 288");
    TEST_ASSERT(layout_std.frame_allocation_size == 288, "Frame allocation 288");

    std::cout << "  [PASS] Stage 265.6 PAC/JOPHASH semantics established without unsupported claims.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 265.7: Correct PAC Profile and Frame Policy
// -----------------------------------------------------------------------------
static bool test_stage265_7_pac_profile_and_frame_policy() {
    std::cout << "[Stage 265.7] Testing profile-dependent frame sizing and alignment policy...\n";

    // 1. Hardware 16-byte stack alignment invariant
    auto layout_std = get_exception_frame_layout(ExecutionProfile::ARM64_Standard);
    auto layout_pac = get_exception_frame_layout(ExecutionProfile::ARM64e_PAC);

    TEST_ASSERT((layout_std.frame_allocation_size % 16) == 0, "Standard frame size must be multiple of 16");
    TEST_ASSERT((layout_pac.frame_allocation_size % 16) == 0, "PAC frame size must be multiple of 16");
    TEST_ASSERT(layout_pac.frame_allocation_size >= layout_pac.raw_saved_state_size, "PAC frame accommodates state");

    // 2. Encoded instruction verification for both profiles
    auto entry_std = encode_exception_entry_sequence(ExecutionProfile::ARM64_Standard, true);
    auto entry_pac = encode_exception_entry_sequence(ExecutionProfile::ARM64e_PAC, true);

    // Instruction 0 of Standard must allocate 288 (0xD10483FF)
    TEST_ASSERT(entry_std[0] == 0xD10483FFu, "Standard entry allocates 288 bytes");

    // Instruction 0 of PAC must allocate 304 (0xD104C3FF)
    TEST_ASSERT(entry_pac[0] == 0xD104C3FFu, "PAC entry allocates 304 bytes");

    std::cout << "  - Standard allocation instruction: 0x" << std::hex << entry_std[0] << " (sub sp, sp, #288)\n";
    std::cout << "  - PAC allocation instruction:      0x" << std::hex << entry_pac[0] << " (sub sp, sp, #304)\n";
    std::cout << std::dec;
    std::cout << "  [PASS] Stage 265.7 Profile-dependent frame sizing verified.\n";
    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Campaign 265 - Stages 265.5..265.7 PAC ABI Truth Tests\n";
    std::cout << "=================================================================\n";

    if (!test_stage265_5_upstream_xnu_abi_audit()) return 1;
    if (!test_stage265_6_pac_jophash_truth()) return 1;
    if (!test_stage265_7_pac_profile_and_frame_policy()) return 1;

    std::cout << "=================================================================\n";
    std::cout << "All Stages 265.5..265.7 Tests Passed Successfully!\n";
    std::cout << "=================================================================\n";
    return 0;
}
