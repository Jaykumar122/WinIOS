#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace winios::cpu {

// =============================================================================
// ARM64 Saved State ABI Specification
// Reference: Apple Public XNU xnu-10002.1.13
// Header: osfmk/mach/arm/thread_status.h
// =============================================================================

// -----------------------------------------------------------------------------
// 1. Upstream Apple XNU Kernel-Private Thread State Flavors
// Defined under #ifdef XNU_KERNEL_PRIVATE in osfmk/mach/arm/thread_status.h:
//   #define ARM_SAVED_STATE32        20
//   #define ARM_SAVED_STATE64        21
//   #define ARM_NEON_SAVED_STATE32   22
//   #define ARM_NEON_SAVED_STATE64   23
// -----------------------------------------------------------------------------
constexpr std::uint32_t ARM_SAVED_STATE32_UPSTREAM       = 20;
constexpr std::uint32_t ARM_SAVED_STATE64_UPSTREAM       = 21;
constexpr std::uint32_t ARM_NEON_SAVED_STATE32_UPSTREAM  = 22;
constexpr std::uint32_t ARM_NEON_SAVED_STATE64_UPSTREAM  = 23;

// -----------------------------------------------------------------------------
// 2. Public Mach Thread State Flavors
// Defined in public section of osfmk/mach/arm/thread_status.h:
// -----------------------------------------------------------------------------
constexpr std::uint32_t ARM_THREAD_STATE                 = 1;
constexpr std::uint32_t ARM_UNIFIED_THREAD_STATE         = 1;
constexpr std::uint32_t ARM_VFP_STATE                    = 2;
constexpr std::uint32_t ARM_EXCEPTION_STATE              = 3;
constexpr std::uint32_t ARM_DEBUG_STATE                  = 4;
constexpr std::uint32_t THREAD_STATE_NONE                = 5;
constexpr std::uint32_t ARM_THREAD_STATE64               = 6;
constexpr std::uint32_t ARM_EXCEPTION_STATE64            = 7;
constexpr std::uint32_t THREAD_STATE_LAST                = 8; // Legacy ARM_THREAD_STATE_LAST
constexpr std::uint32_t ARM_THREAD_STATE32               = 9;
constexpr std::uint32_t ARM_DEBUG_STATE32                = 14;
constexpr std::uint32_t ARM_DEBUG_STATE64                = 15;
constexpr std::uint32_t ARM_NEON_STATE                   = 16;
constexpr std::uint32_t ARM_NEON_STATE64                 = 17;

// -----------------------------------------------------------------------------
// 3. Historical WinIOS & Campaign 259 Compatibility Identifiers
// Documented and retained for explicit audit traceability and test compatibility:
// -----------------------------------------------------------------------------
constexpr std::uint32_t ARM_SAVED_STATE32_CAMPAIGN259    = 9;  // Miscalculated (THREAD_STATE_LAST + 1)
constexpr std::uint32_t ARM_SAVED_STATE64_CAMPAIGN259    = 10; // Miscalculated (THREAD_STATE_LAST + 2)
constexpr std::uint32_t ARM_SAVED_STATE64_WINIOS         = 1;  // WinIOS internal test flavor
constexpr std::uint32_t ARM_SAVED_STATE64                = ARM_SAVED_STATE64_WINIOS;

// Count in natural_t (32-bit words): 288 bytes / 4 bytes = 72 words (non-PAC standard).
constexpr std::uint32_t ARM_SAVED_STATE64_COUNT          = 72;
// ARM64e PAC Word count: (288 + 8) / 4 = 74 words (under HAS_APPLE_PAC).
constexpr std::uint32_t ARM_SAVED_STATE64_PAC_COUNT      = 74;

// Target configuration flag: Apple PAC hardware cryptographic signing engine status
// WinIOS Level 3 emulation target profile executes non-PAC standard ARM64.
constexpr bool kTargetHasApplePac                         = false;

#pragma pack(push, 8)

/// Standard non-PAC 64-bit exception register frame matching Apple XNU struct arm_saved_state64
/// without HAS_APPLE_PAC. Total size is exactly 288 bytes (0x120), 8-byte member alignment.
struct ArmSavedState64 {
    std::uint64_t x[29];    // 0x000 .. 0x0E0: General purpose registers X0..X28 (232 bytes)
    std::uint64_t fp;       // 0x0E8: Frame pointer (X29)
    std::uint64_t lr;       // 0x0F0: Link register (X30)
    std::uint64_t sp;       // 0x0F8: Stack pointer (SP_EL0 or SP_EL1)
    std::uint64_t pc;       // 0x100: Exception Link Register (ELR_EL1)
    std::uint32_t cpsr;     // 0x108: Saved Program Status Register (SPSR_EL1)
    union {
        std::uint32_t reserved; // 0x10C: Reserved padding (named 'reserved' in upstream XNU)
        std::uint32_t pad;      // 0x10C: Historical WinIOS compatibility alias
    };
    std::uint64_t far;      // 0x110: Fault Address Register (FAR_EL1)
    std::uint32_t esr;      // 0x118: Exception Syndrome Register (ESR_EL1, 32-bit)
    std::uint32_t exception;// 0x11C: Exception number / syndrome type (32-bit)
};
using arm_saved_state64_t = ArmSavedState64;
using ArmSavedState64Standard = ArmSavedState64;

/// Authoritative ARM64e PAC 64-bit exception register frame matching Apple XNU struct arm_saved_state64
/// when HAS_APPLE_PAC is enabled. Includes 64-bit jophash field appended at offset 0x120.
/// Total size is exactly 296 bytes (0x128), 8-byte member alignment, 74 natural_t words.
struct ArmSavedState64Pac {
    std::uint64_t x[29];    // 0x000 .. 0x0E0: General purpose registers X0..X28 (232 bytes)
    std::uint64_t fp;       // 0x0E8: Frame pointer (X29)
    std::uint64_t lr;       // 0x0F0: Link register (X30)
    std::uint64_t sp;       // 0x0F8: Stack pointer (SP_EL0 or SP_EL1)
    std::uint64_t pc;       // 0x100: Exception Link Register (ELR_EL1)
    std::uint32_t cpsr;     // 0x108: Saved Program Status Register (SPSR_EL1)
    union {
        std::uint32_t reserved; // 0x10C: Reserved padding
        std::uint32_t pad;      // 0x10C: Historical compatibility alias
    };
    std::uint64_t far;      // 0x110: Fault Address Register (FAR_EL1)
    std::uint32_t esr;      // 0x118: Exception Syndrome Register (ESR_EL1, 32-bit)
    std::uint32_t exception;// 0x11C: Exception number / syndrome type (32-bit)
    std::uint64_t jophash;  // 0x120: JOP/ROP PAC protection hash signature (8 bytes)
};
using arm_saved_state64_pac_t = ArmSavedState64Pac;

/// Upstream arm_state_hdr_t matching Apple XNU osfmk/mach/arm/thread_status.h.
/// Total size is exactly 8 bytes (0x08), 4-byte member alignment.
struct arm_state_hdr_t {
    std::uint32_t flavor;   // 0x00: Thread state flavor (e.g., ARM_SAVED_STATE64_UPSTREAM = 21)
    std::uint32_t count;    // 0x04: Word count in 32-bit natural_t units (72 or 74 words)
};

/// Exact upstream tagged structure matching Apple XNU struct arm_saved_state64_tagged.
/// Defined as: { uint32_t tag; struct arm_saved_state64 state; };
/// Due to 8-byte alignment of state, has 4 bytes padding after tag. Total size: 296 bytes (0x128).
struct ArmSavedState64Tagged {
    std::uint32_t tag;       // 0x000: Tag identifying saved state
    std::uint32_t pad;       // 0x004: Explicit compiler alignment padding
    ArmSavedState64 state;   // 0x008 .. 0x128: 288-byte state frame
};
using arm_saved_state64_tagged_t = ArmSavedState64Tagged;

/// Exact upstream tagged structure for ARM64e PAC target.
/// Total size: 304 bytes (0x130).
struct ArmSavedState64TaggedPac {
    std::uint32_t tag;          // 0x000: Tag identifying saved state
    std::uint32_t pad;          // 0x004: Alignment padding
    ArmSavedState64Pac state;   // 0x008 .. 0x130: 296-byte PAC state frame
};

/// WinIOS direct wrapper container (retained for backward compatibility with existing tests).
/// Total size is exactly 296 bytes (0x128).
struct ArmSavedStateTagged {
    std::uint32_t flavor;   // 0x00: Flavor (1 in WinIOS, 21 upstream)
    std::uint32_t count;    // 0x04: Word count (72 words)
    ArmSavedState64 ss;     // 0x08 .. 0x128: 288-byte state frame
};

#pragma pack(pop)

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif

/// Exact upstream union container matching Apple XNU struct arm_saved_state:
///   struct arm_saved_state {
///       arm_state_hdr_t ash;
///       union {
///           struct arm_saved_state32 ss_32;
///           struct arm_saved_state64 ss_64;
///       } uss;
///   } __attribute__((aligned(16)));
/// In accordance with the 16-byte alignment requirement, sizeof is 304 bytes (0x130)
/// (296 bytes payload + 8 bytes tail padding).
struct alignas(16) ArmSavedState {
    arm_state_hdr_t ash;    // 0x000: Upstream header (flavor + count)
    union {
        ArmSavedState64 ss_64; // 0x008 .. 0x128: 288-byte 64-bit frame
    } uss;
    // 8 bytes tail padding to satisfy alignas(16) constraint (offset 0x128 .. 0x130).
};
using arm_saved_state_t = ArmSavedState;

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// -----------------------------------------------------------------------------
// Compile-time ABI Verifications
// -----------------------------------------------------------------------------
static_assert(ARM_SAVED_STATE32_UPSTREAM == 20, "Upstream ARM_SAVED_STATE32 must be 20");
static_assert(ARM_SAVED_STATE64_UPSTREAM == 21, "Upstream ARM_SAVED_STATE64 must be 21");
static_assert(ARM_NEON_SAVED_STATE32_UPSTREAM == 22, "Upstream ARM_NEON_SAVED_STATE32 must be 22");
static_assert(ARM_NEON_SAVED_STATE64_UPSTREAM == 23, "Upstream ARM_NEON_SAVED_STATE64 must be 23");

static_assert(ARM_THREAD_STATE == 1, "Public ARM_THREAD_STATE must be 1");
static_assert(ARM_THREAD_STATE64 == 6, "Public ARM_THREAD_STATE64 must be 6");
static_assert(THREAD_STATE_LAST == 8, "Legacy THREAD_STATE_LAST must be 8");
static_assert(ARM_SAVED_STATE64_COUNT == 72, "ARM_SAVED_STATE64_COUNT must be 72");

static_assert(sizeof(ArmSavedState64) == 288, "ArmSavedState64 must be exactly 288 bytes (0x120)");
static_assert(alignof(ArmSavedState64) == 8,  "ArmSavedState64 alignment must be 8 bytes");
static_assert(sizeof(ArmSavedState64) / sizeof(std::uint32_t) == ARM_SAVED_STATE64_COUNT,
              "ARM_SAVED_STATE64_COUNT must equal 72 words (288 bytes)");

static_assert(offsetof(ArmSavedState64, x[0])     == 0x000, "x[0] at offset 0x000");
static_assert(offsetof(ArmSavedState64, x[28])    == 0x0E0, "x[28] at offset 0x0E0");
static_assert(offsetof(ArmSavedState64, fp)       == 0x0E8, "fp at offset 0x0E8");
static_assert(offsetof(ArmSavedState64, lr)       == 0x0F0, "lr at offset 0x0F0");
static_assert(offsetof(ArmSavedState64, sp)       == 0x0F8, "sp at offset 0x0F8");
static_assert(offsetof(ArmSavedState64, pc)       == 0x100, "pc at offset 0x100");
static_assert(offsetof(ArmSavedState64, cpsr)     == 0x108, "cpsr at offset 0x108");
static_assert(offsetof(ArmSavedState64, reserved) == 0x10C, "reserved at offset 0x10C");
static_assert(offsetof(ArmSavedState64, far)      == 0x110, "far at offset 0x110");
static_assert(offsetof(ArmSavedState64, esr)      == 0x118, "esr at offset 0x118");
static_assert(offsetof(ArmSavedState64, exception)== 0x11C, "exception at offset 0x11C");

// Header and Container Assertions
static_assert(sizeof(arm_state_hdr_t) == 8, "arm_state_hdr_t must be exactly 8 bytes");
static_assert(alignof(arm_state_hdr_t) == 4, "arm_state_hdr_t alignment must be 4 bytes");
static_assert(offsetof(arm_state_hdr_t, flavor) == 0, "arm_state_hdr_t::flavor at offset 0");
static_assert(offsetof(arm_state_hdr_t, count)  == 4, "arm_state_hdr_t::count at offset 4");

// Upstream ArmSavedState (304 bytes with alignas(16))
static_assert(sizeof(ArmSavedState) == 304, "ArmSavedState must be exactly 304 bytes (0x130)");
static_assert(alignof(ArmSavedState) == 16, "ArmSavedState alignment must be 16 bytes");
static_assert(offsetof(ArmSavedState, ash) == 0, "ArmSavedState::ash at offset 0");
static_assert(offsetof(ArmSavedState, uss.ss_64) == 8, "ArmSavedState::uss.ss_64 at offset 8");

// Upstream ArmSavedState64Tagged (296 bytes with alignof 8)
static_assert(sizeof(ArmSavedState64Tagged) == 296, "ArmSavedState64Tagged must be 296 bytes (0x128)");
static_assert(alignof(ArmSavedState64Tagged) == 8,  "ArmSavedState64Tagged alignment must be 8 bytes");
static_assert(offsetof(ArmSavedState64Tagged, tag) == 0, "ArmSavedState64Tagged::tag at offset 0");
static_assert(offsetof(ArmSavedState64Tagged, state) == 8, "ArmSavedState64Tagged::state at offset 8");

// WinIOS Compatibility ArmSavedStateTagged (296 bytes)
static_assert(sizeof(ArmSavedStateTagged) == 296, "ArmSavedStateTagged must be exactly 296 bytes (0x128)");
static_assert(alignof(ArmSavedStateTagged) == 8,  "ArmSavedStateTagged alignment must be 8 bytes");
static_assert(offsetof(ArmSavedStateTagged, flavor) == 0, "ArmSavedStateTagged::flavor at offset 0");
static_assert(offsetof(ArmSavedStateTagged, count)  == 4, "ArmSavedStateTagged::count at offset 4");
static_assert(offsetof(ArmSavedStateTagged, ss)     == 8, "ArmSavedStateTagged::ss at offset 8");

// Standard layout and trivially copyable assertions
static_assert(std::is_standard_layout_v<ArmSavedState64>, "ArmSavedState64 must be standard layout");
static_assert(std::is_trivially_copyable_v<ArmSavedState64>, "ArmSavedState64 must be trivially copyable");
static_assert(std::is_standard_layout_v<arm_state_hdr_t>, "arm_state_hdr_t must be standard layout");
static_assert(std::is_trivially_copyable_v<arm_state_hdr_t>, "arm_state_hdr_t must be trivially copyable");
static_assert(std::is_standard_layout_v<ArmSavedState>, "ArmSavedState must be standard layout");
static_assert(std::is_trivially_copyable_v<ArmSavedState>, "ArmSavedState must be trivially copyable");
static_assert(std::is_standard_layout_v<ArmSavedState64Tagged>, "ArmSavedState64Tagged must be standard layout");
static_assert(std::is_trivially_copyable_v<ArmSavedState64Tagged>, "ArmSavedState64Tagged must be trivially copyable");
static_assert(std::is_standard_layout_v<ArmSavedStateTagged>, "ArmSavedStateTagged must be standard layout");

// ARM64e PAC Variant Assertions
static_assert(sizeof(ArmSavedState64Pac) == 296, "ArmSavedState64Pac must be exactly 296 bytes (0x128)");
static_assert(alignof(ArmSavedState64Pac) == 8,  "ArmSavedState64Pac alignment must be 8 bytes");
static_assert(offsetof(ArmSavedState64Pac, jophash) == 0x120, "ArmSavedState64Pac::jophash at offset 0x120");
static_assert(sizeof(ArmSavedState64Pac) / sizeof(std::uint32_t) == ARM_SAVED_STATE64_PAC_COUNT,
              "ARM_SAVED_STATE64_PAC_COUNT must equal 74 words (296 bytes)");
static_assert(sizeof(ArmSavedState64TaggedPac) == 304, "ArmSavedState64TaggedPac must be 304 bytes (0x130)");
static_assert(alignof(ArmSavedState64TaggedPac) == 8,  "ArmSavedState64TaggedPac alignment must be 8 bytes");
static_assert(offsetof(ArmSavedState64TaggedPac, tag) == 0, "ArmSavedState64TaggedPac::tag at offset 0");
static_assert(offsetof(ArmSavedState64TaggedPac, state) == 8, "ArmSavedState64TaggedPac::state at offset 8");
static_assert(std::is_standard_layout_v<ArmSavedState64Pac>, "ArmSavedState64Pac must be standard layout");
static_assert(std::is_trivially_copyable_v<ArmSavedState64Pac>, "ArmSavedState64Pac must be trivially copyable");
static_assert(std::is_standard_layout_v<ArmSavedState64TaggedPac>, "ArmSavedState64TaggedPac must be standard layout");
static_assert(std::is_trivially_copyable_v<ArmSavedState64TaggedPac>, "ArmSavedState64TaggedPac must be trivially copyable");
static_assert(std::is_trivially_copyable_v<ArmSavedStateTagged>, "ArmSavedStateTagged must be trivially copyable");

} // namespace winios::cpu
