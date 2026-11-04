#pragma once

#include "trace_types.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace winios::trace {

// -----------------------------------------------------------------------------
// TraceEvent: Fixed 64-byte, cache-line-aligned, trivially-copyable POD record
// -----------------------------------------------------------------------------
// Layout:
//   Offset  0..7   (8B): sequence_number    - Monotonic global event sequence ID
//   Offset  8..15  (8B): virtual_time       - Deterministic virtual execution timestamp
//   Offset 16..23  (8B): pc                 - Guest instruction program counter
//   Offset 24..31  (8B): address            - Primary address (VA, PA, branch target, FAR)
//   Offset 32..39  (8B): data               - Event-specific data payload
//   Offset 40..43  (4B): opcode_or_syndrome - Instruction encoding or exception syndrome (ESR)
//   Offset 44..47  (4B): flags              - Bitmask of TraceEventFlags
//   Offset 48..49  (2B): event_type         - TraceEventType underlying value
//   Offset 50..51  (2B): core_id            - Guest CPU/core identifier
//   Offset 52      (1B): exception_level    - TracePrivilegeLevel underlying value
//   Offset 53      (1B): address_space      - TraceAddressSpace underlying value
//   Offset 54..55  (2B): access_size        - Transaction/access size in bytes (1, 2, 4, 8, etc.)
//   Offset 56..63  (8B): reserved           - Explicit reserved field (must be zero)
// Total Size: Exactly 64 bytes (1 cache line)
// Alignment:  Exactly 64 bytes
// -----------------------------------------------------------------------------
struct alignas(64) TraceEvent {
    std::uint64_t sequence_number{0};
    std::uint64_t virtual_time{0};
    std::uint64_t pc{0};
    std::uint64_t address{0};
    std::uint64_t data{0};
    std::uint32_t opcode_or_syndrome{0};
    std::uint32_t flags{0};
    std::uint16_t event_type{0};
    std::uint16_t core_id{0};
    std::uint8_t  exception_level{0};
    std::uint8_t  address_space{0};
    std::uint16_t access_size{0};
    std::uint64_t reserved{0};

    // Strongly-typed accessors
    [[nodiscard]] constexpr TraceEventType get_event_type() const noexcept {
        return static_cast<TraceEventType>(event_type);
    }

    constexpr void set_event_type(TraceEventType type) noexcept {
        event_type = static_cast<std::uint16_t>(type);
    }

    [[nodiscard]] constexpr TraceEventFlags get_flags() const noexcept {
        return static_cast<TraceEventFlags>(flags);
    }

    constexpr void set_flags(TraceEventFlags f) noexcept {
        flags = static_cast<std::uint32_t>(f);
    }

    [[nodiscard]] constexpr bool has_flag(TraceEventFlags f) const noexcept {
        return (flags & static_cast<std::uint32_t>(f)) == static_cast<std::uint32_t>(f);
    }

    constexpr void add_flag(TraceEventFlags f) noexcept {
        flags |= static_cast<std::uint32_t>(f);
    }

    constexpr void clear_flag(TraceEventFlags f) noexcept {
        flags &= ~static_cast<std::uint32_t>(f);
    }

    [[nodiscard]] constexpr TracePrivilegeLevel get_privilege_level() const noexcept {
        return static_cast<TracePrivilegeLevel>(exception_level);
    }

    constexpr void set_privilege_level(TracePrivilegeLevel el) noexcept {
        exception_level = static_cast<std::uint8_t>(el);
    }

    [[nodiscard]] constexpr TraceAddressSpace get_address_space() const noexcept {
        return static_cast<TraceAddressSpace>(address_space);
    }

    constexpr void set_address_space(TraceAddressSpace as) noexcept {
        address_space = static_cast<std::uint8_t>(as);
    }

    // Equality comparison
    constexpr bool operator==(const TraceEvent& other) const noexcept {
        return sequence_number    == other.sequence_number &&
               virtual_time       == other.virtual_time &&
               pc                 == other.pc &&
               address            == other.address &&
               data               == other.data &&
               opcode_or_syndrome == other.opcode_or_syndrome &&
               flags              == other.flags &&
               event_type         == other.event_type &&
               core_id            == other.core_id &&
               exception_level    == other.exception_level &&
               address_space      == other.address_space &&
               access_size        == other.access_size &&
               reserved           == other.reserved;
    }

    constexpr bool operator!=(const TraceEvent& other) const noexcept {
        return !(*this == other);
    }
};

// -----------------------------------------------------------------------------
// Compile-time Schema Invariants
// -----------------------------------------------------------------------------
static_assert(sizeof(TraceEvent) == 64, "TraceEvent size must be exactly 64 bytes");
static_assert(alignof(TraceEvent) == 64, "TraceEvent alignment must be exactly 64 bytes");
static_assert(std::is_trivially_copyable_v<TraceEvent>, "TraceEvent must be trivially copyable");
static_assert(std::is_standard_layout_v<TraceEvent>, "TraceEvent must be standard layout");

static_assert(offsetof(TraceEvent, sequence_number)    == 0,  "TraceEvent::sequence_number offset mismatch (expected 0)");
static_assert(offsetof(TraceEvent, virtual_time)       == 8,  "TraceEvent::virtual_time offset mismatch (expected 8)");
static_assert(offsetof(TraceEvent, pc)                 == 16, "TraceEvent::pc offset mismatch (expected 16)");
static_assert(offsetof(TraceEvent, address)            == 24, "TraceEvent::address offset mismatch (expected 24)");
static_assert(offsetof(TraceEvent, data)               == 32, "TraceEvent::data offset mismatch (expected 32)");
static_assert(offsetof(TraceEvent, opcode_or_syndrome) == 40, "TraceEvent::opcode_or_syndrome offset mismatch (expected 40)");
static_assert(offsetof(TraceEvent, flags)              == 44, "TraceEvent::flags offset mismatch (expected 44)");
static_assert(offsetof(TraceEvent, event_type)         == 48, "TraceEvent::event_type offset mismatch (expected 48)");
static_assert(offsetof(TraceEvent, core_id)            == 50, "TraceEvent::core_id offset mismatch (expected 50)");
static_assert(offsetof(TraceEvent, exception_level)    == 52, "TraceEvent::exception_level offset mismatch (expected 52)");
static_assert(offsetof(TraceEvent, address_space)      == 53, "TraceEvent::address_space offset mismatch (expected 53)");
static_assert(offsetof(TraceEvent, access_size)        == 54, "TraceEvent::access_size offset mismatch (expected 54)");
static_assert(offsetof(TraceEvent, reserved)           == 56, "TraceEvent::reserved offset mismatch (expected 56)");

static_assert(sizeof(TraceEvent::sequence_number)    == 8, "sequence_number width must be 8 bytes");
static_assert(sizeof(TraceEvent::virtual_time)       == 8, "virtual_time width must be 8 bytes");
static_assert(sizeof(TraceEvent::pc)                 == 8, "pc width must be 8 bytes");
static_assert(sizeof(TraceEvent::address)            == 8, "address width must be 8 bytes");
static_assert(sizeof(TraceEvent::data)               == 8, "data width must be 8 bytes");
static_assert(sizeof(TraceEvent::opcode_or_syndrome) == 4, "opcode_or_syndrome width must be 4 bytes");
static_assert(sizeof(TraceEvent::flags)              == 4, "flags width must be 4 bytes");
static_assert(sizeof(TraceEvent::event_type)         == 2, "event_type width must be 2 bytes");
static_assert(sizeof(TraceEvent::core_id)            == 2, "core_id width must be 2 bytes");
static_assert(sizeof(TraceEvent::exception_level)    == 1, "exception_level width must be 1 byte");
static_assert(sizeof(TraceEvent::address_space)      == 1, "address_space width must be 1 byte");
static_assert(sizeof(TraceEvent::access_size)        == 2, "access_size width must be 2 bytes");
static_assert(sizeof(TraceEvent::reserved)           == 8, "reserved width must be 8 bytes");

} // namespace winios::trace
