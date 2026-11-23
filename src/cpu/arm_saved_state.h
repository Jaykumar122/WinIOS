#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

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
        std::uint32_t fsr;      // 0x10C: Upstream Fault Status Register / ESR_EL1 syndrome
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
        std::uint32_t fsr;      // 0x10C: Upstream Fault Status Register / ESR_EL1 syndrome
    };
    std::uint64_t far;      // 0x110: Fault Address Register (FAR_EL1)
    std::uint32_t esr;      // 0x118: Exception Syndrome Register (ESR_EL1, 32-bit)
    std::uint32_t exception;// 0x11C: Exception number / syndrome type (32-bit)
    std::uint64_t jophash;  // 0x120: JOP/ROP PAC protection hash signature (8 bytes)
};
using arm_saved_state64_pac_t = ArmSavedState64Pac;

/// Exact Upstream Apple XNU arm_saved_state64 matching xnu-10002.1.13 osfmk/mach/arm/thread_status.h
/// Field layout:
///   uint64_t x[29];   // offset 0x000 (0)
///   uint64_t fp;      // offset 0x0E8 (232)
///   uint64_t lr;      // offset 0x0F0 (240)
///   uint64_t sp;      // offset 0x0F8 (248)
///   uint64_t pc;      // offset 0x100 (256)
///   uint32_t cpsr;    // offset 0x108 (264)
///   uint32_t fsr;     // offset 0x10C (268) -> Fault Status Register / ESR syndrome
///   uint64_t far;     // offset 0x110 (272) -> Fault Address Register
///   uint32_t exception;// offset 0x118 (280) -> Exception number / trap code (NOT syndrome!)
///   uint32_t pad;     // offset 0x11C (284) -> Explicit padding
struct arm_saved_state64_upstream {
    std::uint64_t x[29];    // 0x000 .. 0x0E0 (232 bytes)
    std::uint64_t fp;       // 0x0E8 (232)
    std::uint64_t lr;       // 0x0F0 (240)
    std::uint64_t sp;       // 0x0F8 (248)
    std::uint64_t pc;       // 0x100 (256)
    std::uint32_t cpsr;     // 0x108 (264)
    std::uint32_t fsr;      // 0x10C (268): Fault status register (ESR syndrome)
    std::uint64_t far;      // 0x110 (272): Fault address register
    std::uint32_t exception;// 0x118 (280): Exception number / trap type (NOT syndrome)
    std::uint32_t pad;      // 0x11C (284): Alignment padding
};

/// Exact Upstream Apple XNU arm_saved_state64 with HAS_APPLE_PAC enabled.
/// Total size: 296 bytes (74 natural_t words).
struct arm_saved_state64_pac_upstream {
    std::uint64_t x[29];    // 0x000 .. 0x0E0 (232 bytes)
    std::uint64_t fp;       // 0x0E8 (232)
    std::uint64_t lr;       // 0x0F0 (240)
    std::uint64_t sp;       // 0x0F8 (248)
    std::uint64_t pc;       // 0x100 (256)
    std::uint32_t cpsr;     // 0x108 (264)
    std::uint32_t fsr;      // 0x10C (268): Fault status register (ESR syndrome)
    std::uint64_t far;      // 0x110 (272): Fault address register
    std::uint32_t exception;// 0x118 (280): Exception number / trap type (NOT syndrome)
    std::uint32_t pad;      // 0x11C (284): Alignment padding
    std::uint64_t jophash;  // 0x120 (288): JOP/ROP PAC signature under HAS_APPLE_PAC
};

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

// Upstream Exact Layout Static Assertions
static_assert(sizeof(arm_saved_state64_upstream) == 288, "arm_saved_state64_upstream must be 288 bytes");
static_assert(alignof(arm_saved_state64_upstream) == 8, "arm_saved_state64_upstream alignment must be 8");
static_assert(offsetof(arm_saved_state64_upstream, x[0])     == 0x000, "upstream x[0] at 0x000");
static_assert(offsetof(arm_saved_state64_upstream, fp)       == 0x0E8, "upstream fp at 0x0E8");
static_assert(offsetof(arm_saved_state64_upstream, lr)       == 0x0F0, "upstream lr at 0x0F0");
static_assert(offsetof(arm_saved_state64_upstream, sp)       == 0x0F8, "upstream sp at 0x0F8");
static_assert(offsetof(arm_saved_state64_upstream, pc)       == 0x100, "upstream pc at 0x100");
static_assert(offsetof(arm_saved_state64_upstream, cpsr)     == 0x108, "upstream cpsr at 0x108");
static_assert(offsetof(arm_saved_state64_upstream, fsr)      == 0x10C, "upstream fsr at 0x10C (ESR)");
static_assert(offsetof(arm_saved_state64_upstream, far)      == 0x110, "upstream far at 0x110");
static_assert(offsetof(arm_saved_state64_upstream, exception)== 0x118, "upstream exception at 0x118 (NOT syndrome)");
static_assert(offsetof(arm_saved_state64_upstream, pad)      == 0x11C, "upstream pad at 0x11C");

static_assert(sizeof(arm_saved_state64_pac_upstream) == 296, "arm_saved_state64_pac_upstream must be 296 bytes");
static_assert(alignof(arm_saved_state64_pac_upstream) == 8, "arm_saved_state64_pac_upstream alignment must be 8");
static_assert(offsetof(arm_saved_state64_pac_upstream, jophash) == 0x120, "upstream jophash at 0x120");

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

// -----------------------------------------------------------------------------
// Campaign 263/264: Execution Profiles & Authoritative Frame Layout Derivation
// -----------------------------------------------------------------------------
enum class ExecutionProfile : std::uint32_t {
    ARM64_Standard = 0, // ARM64 baseline without Apple PAC (288-byte state)
    ARM64e_PAC     = 1  // ARM64e with Apple PAC (296-byte state, 304-byte aligned frame)
};

constexpr const char* execution_profile_to_string(ExecutionProfile profile) noexcept {
    switch (profile) {
        case ExecutionProfile::ARM64_Standard: return "ARM64_Standard";
        case ExecutionProfile::ARM64e_PAC:     return "ARM64e_PAC";
    }
    return "UnknownProfile";
}

struct ExceptionFrameLayout {
    ExecutionProfile profile{ExecutionProfile::ARM64_Standard};
    std::size_t      raw_saved_state_size{288};
    std::size_t      frame_allocation_size{288}; // 16-byte aligned for ARM64 stack architecture
    std::size_t      saved_state_word_count{72};
    std::size_t      tagged_wrapper_size{296};
    std::size_t      union_wrapper_size{304};
    bool             has_apple_pac{false};

    // Member field offsets within the saved state payload
    std::size_t      gpr_offset{0x000};
    std::size_t      fp_offset{0x0E8};
    std::size_t      lr_offset{0x0F0};
    std::size_t      sp_offset{0x0F8};
    std::size_t      pc_offset{0x100};
    std::size_t      cpsr_offset{0x108};
    std::size_t      fsr_offset{0x10C};            // Upstream FSR / ESR syndrome offset
    std::size_t      far_offset{0x110};
    std::size_t      esr_offset{0x118};            // WinIOS compatibility offset
    std::size_t      exception_code_offset{0x11C}; // WinIOS compatibility offset
    std::size_t      upstream_exception_offset{0x118}; // Upstream exception number offset
    std::size_t      upstream_pad_offset{0x11C};       // Upstream pad offset
    std::size_t      jophash_offset{0x120};        // Only valid if has_apple_pac is true
};

constexpr ExceptionFrameLayout get_exception_frame_layout(ExecutionProfile profile) noexcept {
    if (profile == ExecutionProfile::ARM64e_PAC) {
        // Under HAS_APPLE_PAC:
        // Raw state size = 296 bytes (74 words)
        // Mandatory ARM64 16-byte SP alignment: ceil(296 / 16) * 16 = 304 bytes!
        return ExceptionFrameLayout{
            .profile = ExecutionProfile::ARM64e_PAC,
            .raw_saved_state_size = sizeof(ArmSavedState64Pac),
            .frame_allocation_size = 304,
            .saved_state_word_count = ARM_SAVED_STATE64_PAC_COUNT,
            .tagged_wrapper_size = sizeof(ArmSavedState64TaggedPac),
            .union_wrapper_size = 320,
            .has_apple_pac = true,
            .gpr_offset = offsetof(ArmSavedState64Pac, x[0]),
            .fp_offset = offsetof(ArmSavedState64Pac, fp),
            .lr_offset = offsetof(ArmSavedState64Pac, lr),
            .sp_offset = offsetof(ArmSavedState64Pac, sp),
            .pc_offset = offsetof(ArmSavedState64Pac, pc),
            .cpsr_offset = offsetof(ArmSavedState64Pac, cpsr),
            .fsr_offset = 0x10C,
            .far_offset = offsetof(ArmSavedState64Pac, far),
            .esr_offset = offsetof(ArmSavedState64Pac, esr),
            .exception_code_offset = offsetof(ArmSavedState64Pac, exception),
            .upstream_exception_offset = 0x118,
            .upstream_pad_offset = 0x11C,
            .jophash_offset = offsetof(ArmSavedState64Pac, jophash),
        };
    } else {
        // Standard non-PAC profile:
        // Raw state size = 288 bytes (72 words)
        // 288 % 16 == 0, so frame_allocation_size = 288 bytes!
        return ExceptionFrameLayout{
            .profile = ExecutionProfile::ARM64_Standard,
            .raw_saved_state_size = sizeof(ArmSavedState64),
            .frame_allocation_size = 288,
            .saved_state_word_count = ARM_SAVED_STATE64_COUNT,
            .tagged_wrapper_size = sizeof(ArmSavedStateTagged),
            .union_wrapper_size = sizeof(ArmSavedState),
            .has_apple_pac = false,
            .gpr_offset = offsetof(ArmSavedState64, x[0]),
            .fp_offset = offsetof(ArmSavedState64, fp),
            .lr_offset = offsetof(ArmSavedState64, lr),
            .sp_offset = offsetof(ArmSavedState64, sp),
            .pc_offset = offsetof(ArmSavedState64, pc),
            .cpsr_offset = offsetof(ArmSavedState64, cpsr),
            .fsr_offset = 0x10C,
            .far_offset = offsetof(ArmSavedState64, far),
            .esr_offset = offsetof(ArmSavedState64, esr),
            .exception_code_offset = offsetof(ArmSavedState64, exception),
            .upstream_exception_offset = 0x118,
            .upstream_pad_offset = 0x11C,
            .jophash_offset = 0,
        };
    }
}

static_assert(get_exception_frame_layout(ExecutionProfile::ARM64_Standard).raw_saved_state_size == 288);
static_assert(get_exception_frame_layout(ExecutionProfile::ARM64_Standard).frame_allocation_size == 288);
static_assert(get_exception_frame_layout(ExecutionProfile::ARM64_Standard).frame_allocation_size % 16 == 0);
static_assert(get_exception_frame_layout(ExecutionProfile::ARM64e_PAC).raw_saved_state_size == 296);
static_assert(get_exception_frame_layout(ExecutionProfile::ARM64e_PAC).frame_allocation_size == 304);
static_assert(get_exception_frame_layout(ExecutionProfile::ARM64e_PAC).frame_allocation_size % 16 == 0);
static_assert(get_exception_frame_layout(ExecutionProfile::ARM64e_PAC).jophash_offset == 0x120);

// -----------------------------------------------------------------------------
// Campaign 264: Authoritative Exception Assembly Sequence Encoders
// Eliminates magic constants by deriving all instruction words directly from
// ExceptionFrameLayout. Stores 32-bit fields with STR W to avoid clobbering!
// -----------------------------------------------------------------------------
inline std::vector<std::uint32_t> encode_exception_entry_sequence(ExecutionProfile profile, bool is_el0_origin) {
    const auto layout = get_exception_frame_layout(profile);
    std::vector<std::uint32_t> code;

    // 1. Frame Allocation: sub sp, sp, #frame_size
    // In ARM64: sub sp, sp, #imm (0xD1000000 | (imm << 10) | (31 << 5) | 31)
    const std::uint32_t sub_sp_insn = 0xD10003FFu | (static_cast<std::uint32_t>(layout.frame_allocation_size) << 10u);
    code.push_back(sub_sp_insn);

    // 2. Save X0..X29 using 15 STP instructions
    for (std::uint32_t k = 0; k < 15; ++k) {
        std::uint32_t rt1 = k * 2;
        std::uint32_t rt2 = rt1 + 1;
        std::uint32_t pimm7 = k * 2; // offset / 8
        std::uint32_t insn = 0xA90003E0u | ((pimm7 & 0x7Fu) << 15u) | ((rt2 & 0x1Fu) << 10u) | (rt1 & 0x1Fu);
        code.push_back(insn);
    }

    // 3. Save X30 (LR) at offset 240 (0xF0)
    code.push_back(0xF9007BFEu); // str x30, [sp, #240]

    // 4. Save SP at offset 248 (0xF8)
    if (is_el0_origin) {
        code.push_back(0xD5384100u); // mrs x0, sp_el0
        code.push_back(0xF9007FE0u); // str x0, [sp, #248]
    } else {
        // Pre-allocation SP = sp + frame_size
        const std::uint32_t add_sp_insn = 0x910003E0u | (static_cast<std::uint32_t>(layout.frame_allocation_size) << 10u);
        code.push_back(add_sp_insn);  // add x0, sp, #frame_size
        code.push_back(0xF9007FE0u); // str x0, [sp, #248]
    }

    // 5. Save PC (ELR_EL1) at offset 256 (0x100)
    code.push_back(0xD5384020u); // mrs x0, elr_el1
    code.push_back(0xF90083E0u); // str x0, [sp, #256]

    // 6. Save CPSR (SPSR_EL1) at offset 264 (0x108) using 32-bit STR W1 to avoid clobbering offset 268!
    code.push_back(0xD5384001u); // mrs x1, spsr_el1
    code.push_back(0xB9042FE1u); // str w1, [sp, #264] (32-bit store, offset 264)

    // 7. Save FSR / ESR at offset 268 (0x10C) using 32-bit STR W2
    code.push_back(0xD5385202u); // mrs x2, esr_el1
    code.push_back(0xB9043FE2u); // str w2, [sp, #268] (32-bit store, offset 268)

    // 8. Save FAR at offset 272 (0x110)
    code.push_back(0xD5386003u); // mrs x3, far_el1
    code.push_back(0xF9008BE3u); // str x3, [sp, #272] (64-bit store, offset 272)

    // 9. Save exception metadata at offset 280 (0x118) using 32-bit STR W4
    code.push_back(0x52800024u); // mov w4, #1
    code.push_back(0xB9046FE4u); // str w4, [sp, #280] (32-bit store, offset 280)

    // 10. Save pad at offset 284 (0x11C) using 32-bit STR WZR
    code.push_back(0xB9047FFFu); // str wzr, [sp, #284] (32-bit store, offset 284)

    // 11. If PAC profile: initialize jophash at offset 288 (0x120) using 64-bit STR XZR
    if (layout.has_apple_pac) {
        code.push_back(0xF90093FFu); // str xzr, [sp, #288] (64-bit store, offset 288)
    }

    // 12. Reload scratch registers X0..X4 from saved frame
    code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    code.push_back(0xA9410FE2u); // ldp x2, x3, [sp, #16]
    code.push_back(0xF94013E4u); // ldr x4, [sp, #32]

    return code;
}

inline std::vector<std::uint32_t> encode_exception_restore_sequence(ExecutionProfile profile, bool is_el0_origin) {
    const auto layout = get_exception_frame_layout(profile);
    std::vector<std::uint32_t> code;

    // 1. Reload PC (ELR_EL1) from offset 256
    code.push_back(0xF94083E0u); // ldr x0, [sp, #256]
    code.push_back(0xD5184020u); // msr elr_el1, x0

    // 2. Reload CPSR (SPSR_EL1) from offset 264 using 32-bit LDR W1
    code.push_back(0xB9442FE1u); // ldr w1, [sp, #264] (32-bit load)
    code.push_back(0xD5184001u); // msr spsr_el1, x1

    // 3. If EL0 origin: reload SP_EL0 from offset 248
    if (is_el0_origin) {
        code.push_back(0xF9407FE0u); // ldr x0, [sp, #248]
        code.push_back(0xD5184100u); // msr sp_el0, x0
    }

    // 4. Reload X30 (LR) from offset 240
    code.push_back(0xF9407BFEu); // ldr x30, [sp, #240]

    // 5. Reload X2..X29 using 14 LDP instructions in reverse order
    for (int k = 14; k >= 1; --k) {
        std::uint32_t rt1 = static_cast<std::uint32_t>(k * 2);
        std::uint32_t rt2 = rt1 + 1;
        std::uint32_t pimm7 = static_cast<std::uint32_t>(k * 2);
        std::uint32_t insn = 0xA94003E0u | ((pimm7 & 0x7Fu) << 15u) | ((rt2 & 0x1Fu) << 10u) | (rt1 & 0x1Fu);
        code.push_back(insn);
    }

    // 6. Reload X0, X1 from offset 0
    code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]

    // 7. Frame Deallocation: add sp, sp, #frame_size
    const std::uint32_t add_sp_insn = 0x910003FFu | (static_cast<std::uint32_t>(layout.frame_allocation_size) << 10u);
    code.push_back(add_sp_insn);

    // 8. ERET: Return from exception
    code.push_back(0xD69F03E0u); // eret

    return code;
}

} // namespace winios::cpu
