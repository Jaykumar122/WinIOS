#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace winios::trace {

// -----------------------------------------------------------------------------
// Schema Versioning
// -----------------------------------------------------------------------------
constexpr std::uint16_t TRACE_SCHEMA_VERSION_MAJOR = 1;
constexpr std::uint16_t TRACE_SCHEMA_VERSION_MINOR = 0;
constexpr std::uint32_t TRACE_SCHEMA_VERSION = 
    (static_cast<std::uint32_t>(TRACE_SCHEMA_VERSION_MAJOR) << 16) | 
    static_cast<std::uint32_t>(TRACE_SCHEMA_VERSION_MINOR);

// -----------------------------------------------------------------------------
// TraceEventType: Fixed 16-bit enumeration of architectural and diagnostic events
// -----------------------------------------------------------------------------
enum class TraceEventType : std::uint16_t {
    Invalid                 = 0,
    InstructionRetired      = 1,
    BranchTaken             = 2,
    ExceptionEntered        = 3,
    ExceptionReturned       = 4,
    InterruptDelivered      = 5,
    SystemRegisterAccess    = 6,
    MemoryRead              = 7,
    MemoryWrite             = 8,
    DebugEvent              = 9,
    CPUStarted              = 10,
    CPUStopped              = 11,
    ContextChanged          = 12,
    TimerEvent              = 13,

    // Reserved Architectural Range (14..99)
    ReservedArchStart       = 14,
    ReservedArchEnd         = 99,

    // Reserved Subsystem / Micro-architectural Range (100..199)
    ReservedMicroarchStart  = 100,
    ReservedMicroarchEnd    = 199,

    // Diagnostic & Synthetic Markers (200..255)
    DroppedMarker           = 200,
    TraceStart              = 201,
    TraceEnd                = 202,
    ReservedDiagStart       = 203,
    ReservedDiagEnd         = 255,

    // Reserved Future Extension Range (256..65535)
    ReservedFutureStart     = 256,
    ReservedFutureEnd       = 0xFFFF
};

// -----------------------------------------------------------------------------
// TraceEventFlags: 32-bit bitmask describing event attributes and validity
// -----------------------------------------------------------------------------
enum class TraceEventFlags : std::uint32_t {
    None                = 0x00000000u,
    HasVirtualAddress   = 1u << 0,  // address field contains a valid Virtual Address
    HasPhysicalAddress  = 1u << 1,  // secondary address or decoded address is Physical
    HasData             = 1u << 2,  // data payload field contains valid access/register data
    IsPrivileged        = 1u << 3,  // executed at EL > 0 (EL1, EL2, EL3)
    IsWrite             = 1u << 4,  // memory store or register write (vs read)
    IsTaken             = 1u << 5,  // branch was taken (vs untaken conditional branch)
    IsSynchronous       = 1u << 6,  // synchronous exception/trap (vs async interrupt)
    IsExceptionReturn   = 1u << 7,  // exception exit / ERET
    IsCompressed        = 1u << 8,  // compressed instruction encoding or trace stream block
    IsInterrupt         = 1u << 9,  // physical or virtual IRQ/FIQ interrupt
    IsSingleStep        = 1u << 10, // hardware single-step debug event
    IsBreakpoint        = 1u << 11, // hardware execution breakpoint debug event
    IsWatchpoint        = 1u << 12, // hardware data watchpoint debug event
    IsAtomic            = 1u << 13, // exclusive/atomic memory transaction (LDXR/STXR/CAS)
    IsDroppedMarker     = 1u << 14  // synthetic event indicating dropped event count
};

// Bitwise operators for TraceEventFlags
constexpr inline TraceEventFlags operator|(TraceEventFlags a, TraceEventFlags b) noexcept {
    return static_cast<TraceEventFlags>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr inline TraceEventFlags operator&(TraceEventFlags a, TraceEventFlags b) noexcept {
    return static_cast<TraceEventFlags>(
        static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

constexpr inline TraceEventFlags operator^(TraceEventFlags a, TraceEventFlags b) noexcept {
    return static_cast<TraceEventFlags>(
        static_cast<std::uint32_t>(a) ^ static_cast<std::uint32_t>(b));
}

constexpr inline TraceEventFlags operator~(TraceEventFlags a) noexcept {
    return static_cast<TraceEventFlags>(~static_cast<std::uint32_t>(a));
}

constexpr inline TraceEventFlags& operator|=(TraceEventFlags& a, TraceEventFlags b) noexcept {
    a = a | b;
    return a;
}

constexpr inline TraceEventFlags& operator&=(TraceEventFlags& a, TraceEventFlags b) noexcept {
    a = a & b;
    return a;
}

constexpr inline TraceEventFlags& operator^=(TraceEventFlags& a, TraceEventFlags b) noexcept {
    a = a ^ b;
    return a;
}

constexpr inline bool has_flag(TraceEventFlags flags, TraceEventFlags test) noexcept {
    return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(test)) == static_cast<std::uint32_t>(test);
}

// -----------------------------------------------------------------------------
// TraceExecutionMode: Host execution scheduling mode
// -----------------------------------------------------------------------------
enum class TraceExecutionMode : std::uint8_t {
    Deterministic = 0, // Single-threaded, deterministic round-robin / quantum lockstep
    Parallel      = 1  // Multi-threaded, per-core worker threads with SPSC queues
};

// -----------------------------------------------------------------------------
// TracePrivilegeLevel: Architectural AArch64 Exception Levels
// -----------------------------------------------------------------------------
enum class TracePrivilegeLevel : std::uint8_t {
    EL0 = 0, // User / unprivileged
    EL1 = 1, // Kernel / OS supervisor
    EL2 = 2, // Hypervisor
    EL3 = 3  // Secure monitor / firmware
};

// -----------------------------------------------------------------------------
// TraceAddressSpace: Address space classification
// -----------------------------------------------------------------------------
enum class TraceAddressSpace : std::uint8_t {
    None                = 0, // Address field not applicable or invalid
    Virtual             = 1, // Address represents Guest Virtual Address (VA)
    Physical            = 2, // Address represents Guest Intermediate / Physical Address (IPA/PA)
    VirtualAndPhysical  = 3  // Address represents VA, secondary address represents PA
};

// -----------------------------------------------------------------------------
// Compile-time Static Assertions on Types
// -----------------------------------------------------------------------------
static_assert(sizeof(TraceEventType) == 2, "TraceEventType must be exactly 2 bytes (uint16_t)");
static_assert(sizeof(TraceEventFlags) == 4, "TraceEventFlags must be exactly 4 bytes (uint32_t)");
static_assert(sizeof(TraceExecutionMode) == 1, "TraceExecutionMode must be exactly 1 byte (uint8_t)");
static_assert(sizeof(TracePrivilegeLevel) == 1, "TracePrivilegeLevel must be exactly 1 byte (uint8_t)");
static_assert(sizeof(TraceAddressSpace) == 1, "TraceAddressSpace must be exactly 1 byte (uint8_t)");

static_assert(std::is_enum_v<TraceEventType>, "TraceEventType must be an enum");
static_assert(std::is_enum_v<TraceEventFlags>, "TraceEventFlags must be an enum");
static_assert(std::is_enum_v<TraceExecutionMode>, "TraceExecutionMode must be an enum");
static_assert(std::is_enum_v<TracePrivilegeLevel>, "TracePrivilegeLevel must be an enum");
static_assert(std::is_enum_v<TraceAddressSpace>, "TraceAddressSpace must be an enum");

// -----------------------------------------------------------------------------
// Diagnostic String Conversion Helpers
// -----------------------------------------------------------------------------
constexpr inline std::string_view to_string(TraceEventType type) noexcept {
    switch (type) {
        case TraceEventType::Invalid:              return "Invalid";
        case TraceEventType::InstructionRetired:   return "InstructionRetired";
        case TraceEventType::BranchTaken:          return "BranchTaken";
        case TraceEventType::ExceptionEntered:     return "ExceptionEntered";
        case TraceEventType::ExceptionReturned:    return "ExceptionReturned";
        case TraceEventType::InterruptDelivered:   return "InterruptDelivered";
        case TraceEventType::SystemRegisterAccess: return "SystemRegisterAccess";
        case TraceEventType::MemoryRead:           return "MemoryRead";
        case TraceEventType::MemoryWrite:          return "MemoryWrite";
        case TraceEventType::DebugEvent:           return "DebugEvent";
        case TraceEventType::CPUStarted:           return "CPUStarted";
        case TraceEventType::CPUStopped:           return "CPUStopped";
        case TraceEventType::ContextChanged:       return "ContextChanged";
        case TraceEventType::TimerEvent:           return "TimerEvent";
        case TraceEventType::DroppedMarker:        return "DroppedMarker";
        case TraceEventType::TraceStart:           return "TraceStart";
        case TraceEventType::TraceEnd:             return "TraceEnd";
        default:                                   return "Unknown";
    }
}

constexpr inline std::string_view to_string(TracePrivilegeLevel el) noexcept {
    switch (el) {
        case TracePrivilegeLevel::EL0: return "EL0";
        case TracePrivilegeLevel::EL1: return "EL1";
        case TracePrivilegeLevel::EL2: return "EL2";
        case TracePrivilegeLevel::EL3: return "EL3";
        default:                       return "UnknownEL";
    }
}

constexpr inline std::string_view to_string(TraceAddressSpace as) noexcept {
    switch (as) {
        case TraceAddressSpace::None:               return "None";
        case TraceAddressSpace::Virtual:            return "Virtual";
        case TraceAddressSpace::Physical:           return "Physical";
        case TraceAddressSpace::VirtualAndPhysical: return "VirtualAndPhysical";
        default:                                    return "UnknownAddressSpace";
    }
}

constexpr inline std::string_view to_string(TraceExecutionMode mode) noexcept {
    switch (mode) {
        case TraceExecutionMode::Deterministic: return "Deterministic";
        case TraceExecutionMode::Parallel:      return "Parallel";
        default:                                return "UnknownMode";
    }
}

} // namespace winios::trace
