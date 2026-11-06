#pragma once

// =============================================================================
// WinIOS Phase 52 Stage 37: Darwin System-Register Compatibility Layer
// =============================================================================
// RESEARCH FOUNDATION: This module maintains an isolated inventory and research
// registry of system registers relevant to Apple Silicon and Darwin/XNU execution.
//
// ISOLATION CONTRACT:
// 1. Research-only and platform-specific entries are strictly isolated here and
//    ARE NOT mixed into the active CPU execution registry.
// 2. The active CPU continues to reject unsupported system registers with standard
//    AArch64 Undefined Instruction exceptions (EC=0x00).
// 3. Any Darwin compatibility profile behavior requires explicit opt-in and does
//    not alter default deterministic CPU execution semantics.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace winios::darwin {

// -----------------------------------------------------------------------------
// Classification & Status Enums
// -----------------------------------------------------------------------------
enum class Classification : std::uint8_t {
    GenericArchitectural,        // Standard Armv8-A system register
    PubliclyDocumentedDarwin,    // Publicly documented Darwin/XNU requirement
    ResearchOnly,                // Discovered via public reverse engineering / research
    Unsupported,                 // Explicitly rejected in WinIOS
    Unknown                      // Undocumented / unclassified
};

enum class Confidence : std::uint8_t {
    High,        // Documented in Arm Architecture Reference Manual or Apple Open Source
    Medium,      // Documented in public open-source bootloaders (m1n1 / Asahi Linux)
    Low,         // Inferred from compiler/ABI disassembly without formal public specification
    Speculative  // Hypothesized behavior
};

enum class CompatibilityStatus : std::uint8_t {
    FullyImplemented,       // Modeled and active in WinIOS CPU core
    PartiallyImplemented,   // Basic storage or read-as-zero modeled
    Stubbed,                // Defined in compatibility layer only
    Deferred,               // Explicitly deferred to future phases
    Unsupported             // Rejected with architectural exception
};

// -----------------------------------------------------------------------------
// Compatibility Register Entry
// -----------------------------------------------------------------------------
struct DarwinSysRegEntry {
    const char* name;
    std::uint16_t key;
    std::uint8_t op0;
    std::uint8_t op1;
    std::uint8_t crn;
    std::uint8_t crm;
    std::uint8_t op2;
    std::uint8_t min_el;
    bool can_read;
    bool can_write;
    std::uint64_t reset_value;
    Classification classification;
    Confidence confidence;
    CompatibilityStatus status;
    const char* evidence_source;
    const char* architectural_role;
};

// -----------------------------------------------------------------------------
// Darwin Compatibility Profile
// -----------------------------------------------------------------------------
enum class ProfileMode : std::uint8_t {
    StandardAArch64,      // Default: Pure generic AArch64 execution (no Darwin overrides)
    DarwinResearchAudit   // Audit mode: Enables logging of Darwin-relevant register access patterns
};

struct DarwinProfileConfig {
    ProfileMode mode{ProfileMode::StandardAArch64};
    bool allow_actlr_el1_rw{false};
    bool log_unimplemented_darwin_access{true};

    [[nodiscard]] std::uint64_t compute_profile_hash() const noexcept;
};

// -----------------------------------------------------------------------------
// Compatibility Registry Interface
// -----------------------------------------------------------------------------
class DarwinCompatibilityRegistry {
public:
    static std::span<const DarwinSysRegEntry> get_all_entries() noexcept;
    static const DarwinSysRegEntry* find_by_key(std::uint16_t key) noexcept;
    static const DarwinSysRegEntry* find_by_name(std::string_view name) noexcept;
    static std::size_t get_entry_count() noexcept;

    // Classification filter queries
    static std::size_t count_by_classification(Classification c) noexcept;
    static std::size_t count_by_status(CompatibilityStatus s) noexcept;
};

} // namespace winios::darwin
