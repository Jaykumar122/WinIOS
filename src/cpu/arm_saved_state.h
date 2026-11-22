#pragma once

#include <cstddef>
#include <cstdint>

namespace winios::cpu {

// =============================================================================
// ARM64 Saved State ABI Specification
// Reference: Apple Public XNU xnu-10002.1.13
// Header: osfmk/mach/arm/thread_status.h (arm_saved_state64_t)
// =============================================================================

// Flavor identifiers matching upstream Apple XNU osfmk/mach/arm/thread_status.h
constexpr std::uint32_t ARM_SAVED_STATE64 = 1;
// Count in natural_t (32-bit words): 288 bytes / 4 bytes = 72 words (previously misstated as 68).
constexpr std::uint32_t ARM_SAVED_STATE64_COUNT = 72;

#pragma pack(push, 8)

/// Exact 64-bit exception register frame matching Apple XNU arm_saved_state64_t.
/// Total size is exactly 288 bytes (0x120).
struct ArmSavedState64 {
    std::uint64_t x[29];    // 0x000 .. 0x0E0: General purpose registers X0..X28 (232 bytes)
    std::uint64_t fp;       // 0x0E8: Frame pointer (X29)
    std::uint64_t lr;       // 0x0F0: Link register (X30)
    std::uint64_t sp;       // 0x0F8: Stack pointer (SP_EL0 or SP_EL1)
    std::uint64_t pc;       // 0x100: Exception Link Register (ELR_EL1)
    std::uint32_t cpsr;     // 0x108: Saved Program Status Register (SPSR_EL1)
    std::uint32_t pad;      // 0x10C: 64-bit alignment padding
    std::uint64_t far;      // 0x110: Fault Address Register (FAR_EL1)
    std::uint32_t esr;      // 0x118: Exception Syndrome Register (ESR_EL1)
    std::uint32_t exception;// 0x11C: Exception code / syndrome type
};

/// Tagged container matching Apple XNU arm_saved_state_t header + 64-bit state.
/// Upstream reference: osfmk/mach/arm/thread_status.h struct arm_saved_state
/// arm_state_hdr_t ash: flavor (ARM_SAVED_STATE64 = 1), count (ARM_SAVED_STATE64_COUNT = 72 words)
/// Total size is exactly 296 bytes (0x128).
struct ArmSavedStateTagged {
    std::uint32_t flavor;   // 0x00: ARM_SAVED_STATE64 (1)
    std::uint32_t count;    // 0x04: ARM_SAVED_STATE64_COUNT (72 words)
    ArmSavedState64 ss;     // 0x08 .. 0x128: 288-byte state frame
};

#pragma pack(pop)

// -----------------------------------------------------------------------------
// Compile-time ABI Verifications
// -----------------------------------------------------------------------------
static_assert(sizeof(ArmSavedState64) == 288, "ArmSavedState64 must be exactly 288 bytes (0x120)");
static_assert(alignof(ArmSavedState64) == 8,  "ArmSavedState64 alignment must be 8 bytes");
static_assert(sizeof(ArmSavedState64) / sizeof(std::uint32_t) == ARM_SAVED_STATE64_COUNT,
              "ARM_SAVED_STATE64_COUNT must equal 72 words (288 bytes)");
static_assert(ARM_SAVED_STATE64_COUNT == 72, "ARM_SAVED_STATE64_COUNT must be 72");

static_assert(offsetof(ArmSavedState64, x[0])     == 0x000, "x[0] at offset 0x000");
static_assert(offsetof(ArmSavedState64, x[28])    == 0x0E0, "x[28] at offset 0x0E0");
static_assert(offsetof(ArmSavedState64, fp)       == 0x0E8, "fp at offset 0x0E8");
static_assert(offsetof(ArmSavedState64, lr)       == 0x0F0, "lr at offset 0x0F0");
static_assert(offsetof(ArmSavedState64, sp)       == 0x0F8, "sp at offset 0x0F8");
static_assert(offsetof(ArmSavedState64, pc)       == 0x100, "pc at offset 0x100");
static_assert(offsetof(ArmSavedState64, cpsr)     == 0x108, "cpsr at offset 0x108");
static_assert(offsetof(ArmSavedState64, pad)      == 0x10C, "pad at offset 0x10C");
static_assert(offsetof(ArmSavedState64, far)      == 0x110, "far at offset 0x110");
static_assert(offsetof(ArmSavedState64, esr)      == 0x118, "esr at offset 0x118");
static_assert(offsetof(ArmSavedState64, exception)== 0x11C, "exception at offset 0x11C");

static_assert(sizeof(ArmSavedStateTagged) == 296, "ArmSavedStateTagged must be exactly 296 bytes (0x128)");
static_assert(offsetof(ArmSavedStateTagged, ss) == 0x008, "ArmSavedStateTagged::ss at offset 0x008");

} // namespace winios::cpu
