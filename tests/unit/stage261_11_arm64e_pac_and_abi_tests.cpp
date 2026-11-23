#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "cpu/arm_saved_state.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::cpu;

// ----------------------------------------------------------------------------
// Test 1: Stage 261.9: Exact Upstream Apple XNU ABI Constants & Word Counts
// ----------------------------------------------------------------------------
static void test_stage261_9_upstream_abi_audit() {
    std::cout << "[Stage 261.9] Testing Exact Upstream Apple XNU ABI Constants...\n";

    // 1. Kernel-private internal flavors (osfmk/mach/arm/thread_status.h under #ifdef XNU_KERNEL_PRIVATE)
    TEST_ASSERT(ARM_SAVED_STATE32_UPSTREAM == 20, "ARM_SAVED_STATE32_UPSTREAM must be 20");
    TEST_ASSERT(ARM_SAVED_STATE64_UPSTREAM == 21, "ARM_SAVED_STATE64_UPSTREAM must be 21");
    TEST_ASSERT(ARM_NEON_SAVED_STATE32_UPSTREAM == 22, "ARM_NEON_SAVED_STATE32_UPSTREAM must be 22");
    TEST_ASSERT(ARM_NEON_SAVED_STATE64_UPSTREAM == 23, "ARM_NEON_SAVED_STATE64_UPSTREAM must be 23");

    // 2. Public thread state flavors
    TEST_ASSERT(ARM_THREAD_STATE == 1, "Public ARM_THREAD_STATE must be 1");
    TEST_ASSERT(ARM_THREAD_STATE64 == 6, "Public ARM_THREAD_STATE64 must be 6");
    TEST_ASSERT(ARM_THREAD_STATE32 == 9, "Public ARM_THREAD_STATE32 must be 9");
    TEST_ASSERT(THREAD_STATE_LAST == 8, "Legacy THREAD_STATE_LAST must be 8");

    // 3. Historical Campaign 259 constants (preserved for retrospective audit)
    TEST_ASSERT(ARM_SAVED_STATE32_CAMPAIGN259 == 9, "Campaign 259 legacy constant 9 preserved");
    TEST_ASSERT(ARM_SAVED_STATE64_CAMPAIGN259 == 10, "Campaign 259 legacy constant 10 preserved");

    // 4. Header representation
    arm_state_hdr_t hdr{};
    hdr.flavor = ARM_SAVED_STATE64_UPSTREAM;
    hdr.count  = ARM_SAVED_STATE64_COUNT;
    TEST_ASSERT(hdr.flavor == 21, "Header flavor is 21");
    TEST_ASSERT(hdr.count  == 72, "Header count is 72 natural_t words (288 bytes)");

    std::cout << "[Stage 261.9] Upstream Apple XNU ABI audit verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 261.10: ARM64e PAC ABI Resolution & Comparison
// ----------------------------------------------------------------------------
static void test_stage261_10_arm64e_pac_abi_resolution() {
    std::cout << "[Stage 261.10] Testing ARM64e PAC vs Non-PAC ABI Resolution...\n";

    // 1. Non-PAC Standard Layout (ArmSavedState64 / ArmSavedState64Standard)
    TEST_ASSERT(sizeof(ArmSavedState64Standard) == 288, "Non-PAC standard layout is exactly 288 bytes (0x120)");
    TEST_ASSERT(alignof(ArmSavedState64Standard) == 8, "Non-PAC alignment is 8 bytes");
    TEST_ASSERT(ARM_SAVED_STATE64_COUNT == 72, "Non-PAC word count is exactly 72 words");

    // 2. PAC Layout (ArmSavedState64Pac under HAS_APPLE_PAC)
    // Upstream XNU appends: uint64_t jophash; at offset 0x120
    TEST_ASSERT(sizeof(ArmSavedState64Pac) == 296, "PAC layout is exactly 296 bytes (0x128)");
    TEST_ASSERT(alignof(ArmSavedState64Pac) == 8, "PAC alignment is 8 bytes");
    TEST_ASSERT(offsetof(ArmSavedState64Pac, jophash) == 0x120, "PAC jophash field is at offset 0x120 (288)");
    TEST_ASSERT(ARM_SAVED_STATE64_PAC_COUNT == 74, "PAC word count is exactly 74 words (296 / 4 = 74)");

    // 3. Tagged Wrappers Comparison
    // Non-PAC tagged: tag (4) + pad (4) + ArmSavedState64 (288) = 296 bytes
    TEST_ASSERT(sizeof(ArmSavedState64Tagged) == 296, "Non-PAC tagged wrapper is 296 bytes (0x128)");
    TEST_ASSERT(offsetof(ArmSavedState64Tagged, state) == 8, "Non-PAC state begins at offset 8");

    // PAC tagged: tag (4) + pad (4) + ArmSavedState64Pac (296) = 304 bytes
    TEST_ASSERT(sizeof(ArmSavedState64TaggedPac) == 304, "PAC tagged wrapper is 304 bytes (0x130)");
    TEST_ASSERT(offsetof(ArmSavedState64TaggedPac, state) == 8, "PAC state begins at offset 8");

    // 4. Target Architecture Baseline Assessment:
    // Report target configuration: WinIOS Level 3 emulation baseline targets non-PAC standard ARM64.
    TEST_ASSERT(kTargetHasApplePac == false, "Target configuration established: Non-PAC baseline (288 B)");

    std::cout << "[Stage 261.10] ARM64e PAC vs Non-PAC ABI comparison and target resolution verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 261.11: Compile-time and Runtime ABI Verification
// ----------------------------------------------------------------------------
static void test_stage261_11_runtime_abi_and_serialization() {
    std::cout << "[Stage 261.11] Testing Runtime ABI, Field Offsets & Serialization...\n";

    ArmSavedState64 state{};
    // Initialize registers with identifiable unique patterns
    for (uint32_t i = 0; i < 29; ++i) {
        state.x[i] = 0x1000000000000000ULL | (static_cast<uint64_t>(i) << 32) | 0xAA;
    }
    state.fp = 0xAAAAAAAAAAAAAAAAULL;
    state.lr = 0xBBBBBBBBBBBBBBBBULL;
    state.sp = 0x00044000ULL;
    state.pc = 0x0000100CULL;
    state.cpsr = 0x60000000u;
    state.reserved = 0u;
    state.far = 0xFFFF800012340000ULL;
    state.esr = 0x56000000u;
    state.exception = 1u;

    // Verify memory buffer bitwise copy
    std::vector<uint8_t> serialized(sizeof(state));
    std::memcpy(serialized.data(), &state, sizeof(state));

    ArmSavedState64 deserialized{};
    std::memcpy(&deserialized, serialized.data(), sizeof(deserialized));

    for (uint32_t i = 0; i < 29; ++i) {
        TEST_ASSERT(deserialized.x[i] == state.x[i], "X[i] matches after serialization round-trip");
    }
    TEST_ASSERT(deserialized.fp == state.fp, "FP matches");
    TEST_ASSERT(deserialized.lr == state.lr, "LR matches");
    TEST_ASSERT(deserialized.sp == state.sp, "SP matches");
    TEST_ASSERT(deserialized.pc == state.pc, "PC matches");
    TEST_ASSERT(deserialized.cpsr == state.cpsr, "CPSR matches");
    TEST_ASSERT(deserialized.far == state.far, "FAR matches");
    TEST_ASSERT(deserialized.esr == state.esr, "ESR matches");
    TEST_ASSERT(deserialized.exception == state.exception, "Exception matches");

    // PAC Serialization Round-Trip
    ArmSavedState64Pac pac_state{};
    std::memcpy(&pac_state, &state, sizeof(state));
    pac_state.jophash = 0x123456789ABCDEF0ULL;

    std::vector<uint8_t> pac_buf(sizeof(pac_state));
    std::memcpy(pac_buf.data(), &pac_state, sizeof(pac_state));

    ArmSavedState64Pac pac_deserialized{};
    std::memcpy(&pac_deserialized, pac_buf.data(), sizeof(pac_deserialized));
    TEST_ASSERT(pac_deserialized.jophash == 0x123456789ABCDEF0ULL, "PAC jophash restored correctly");
    TEST_ASSERT(pac_deserialized.pc == state.pc, "PAC PC restored correctly");

    std::cout << "[Stage 261.11] Runtime ABI and serialization verified.\n";
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    try {
        std::cout << "====================================================================\n";
        std::cout << "Campaign 261: Stages 261.9 - 261.11 ARM64e PAC & ABI Tests\n";
        std::cout << "====================================================================\n";

        test_stage261_9_upstream_abi_audit();
        test_stage261_10_arm64e_pac_abi_resolution();
        test_stage261_11_runtime_abi_and_serialization();

        std::cout << "====================================================================\n";
        std::cout << "All Stages 261.9 - 261.11 Tests PASSED Successfully!\n";
        std::cout << "====================================================================\n";
    } catch (const std::exception& e) {
        std::cerr << "FATAL EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}