#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Phase 52 Stage 39: bounded AArch64 architectural debug subsystem.
// This subsystem provides per-core debug state, breakpoint and watchpoint
// register management, address matching logic, control-encoding validation,
// software single-step integration, and debug event reporting.
//
// This is the ARCHITECTURAL debug interface (guest-visible debug registers and
// debug exceptions), separate from the software debugger (host-side stepping
// and inspection).
//
// Implementation scope:
//   - 4 hardware execution breakpoints (DBGBVR/DBGBCR)
//   - 4 hardware data watchpoints (DBGWVR/DBGWCR)
//   - Virtual address matching (before MMU translation)
//   - Privilege/EL filtering
//   - Load/Store/Both watchpoint types
//   - Byte Address Select (BAS) for sub-word watchpoint matching
//   - Debug enable/disable control (MDSCR_EL1.MDE)
//   - Software step control (MDSCR_EL1.SS) with the CPU pipeline
//   - MDCCINT_EL1 metadata (no DCC peer)
//   - Explicit support classification of every control encoding
//
// Not implemented (documented limitations):
//   - Physical address matching
//   - Context ID matching / BT=2
//   - HMC, SSC, and watchpoint MASK filtering
//   - External debug interface / halting debug mode
//   - Debug Communication Channel data transfer and interrupts

namespace winios {

// Number of hardware breakpoint and watchpoint registers.
// This value is reported in ID_AA64DFR0_EL1.
constexpr std::size_t kDebugBreakpointCount = 4;
constexpr std::size_t kDebugWatchpointCount = 4;

// Debug event type for reporting to the exception subsystem.
enum class DebugEventType : std::uint32_t {
    None = 0,
    Breakpoint = 1,
    Watchpoint = 2,
    SoftwareStep = 3,  // Stage 39: software single-step debug event
};

// Memory access type for watchpoint matching.
enum class DebugAccessType : std::uint32_t {
    Read = 0,
    Write = 1,
};

// Privilege/exception level filtering for breakpoint and watchpoint matching.
// Maps to DBGBCR.PMC and DBGWCR.PAC fields.
enum class DebugPrivilegeMode : std::uint32_t {
    None = 0,   // No filtering (matches all ELs)
    EL1 = 1,    // Match only EL1
    EL0 = 2,    // Match only EL0
    EL0_EL1 = 3,// Match EL0 or EL1
};

// Watchpoint load/store control (DBGWCR.LSC field).
enum class WatchpointLoadStoreControl : std::uint32_t {
    Neither = 0,  // Reserved; treated as disabled
    Load = 1,     // Match load/read operations
    Store = 2,    // Match store/write operations
    Both = 3,     // Match both load and store operations
};

// Stage 39: support classification for a breakpoint/watchpoint control value.
//
// The architecture reserves several DBGBCR_EL1/DBGWCR_EL1 encodings that this
// bounded implementation does not model (HMC, SSC, MASK, linked context ID
// matching).  Rather than silently ignoring those fields - which would turn an
// unsupported request into an unexpected debug exception - every control value
// is classified and a slot whose encoding is not fully supported is marked
// inactive.
enum class DebugControlSupport : std::uint32_t {
    Disabled = 0,          // E == 0: slot is architecturally disabled.
    Supported = 1,         // Modeling exactly matches the requested encoding.
    UnsupportedFeature = 2,// Recognized Arm feature outside this bounded subset.
    ReservedEncoding = 3,  // Reserved/undefined field encoding (e.g. BT > 1).
};

[[nodiscard]] const char* debug_control_support_name(DebugControlSupport support) noexcept;

// Stage 39: decoded view of a DBGBCR_EL1/DBGWCR_EL1 control value.
// `active` is the single authoritative predicate used by the matching logic:
// it is true only when the slot is enabled AND the entire encoding is modeled.
struct DebugControlDecode {
    bool enabled{false};
    DebugPrivilegeMode privilege_mode{DebugPrivilegeMode::None};
    bool hmc{false};                       // bit 13 (unmodeled)
    std::uint8_t ssc{0};                    // bits [15:14] (unmodeled)
    std::uint8_t bas{0};                    // breakpoints: [8:5]; watchpoints: [12:5]
    std::uint8_t lbn{0};                    // bits [19:16]
    std::uint8_t type{0};                   // breakpoints: BT [23:20]
    std::uint8_t mask{0};                   // watchpoints: MASK [28:24] (unmodeled)
    WatchpointLoadStoreControl lsc{WatchpointLoadStoreControl::Neither};
    bool linked{false};                     // watchpoint WT (bit 20)
    DebugControlSupport support{DebugControlSupport::Disabled};
    bool active{false};
    const char* reason{"disabled"};
};

// Breakpoint register state (DBGBVR<n>_EL1 and DBGBCR<n>_EL1).
struct BreakpointRegister {
    std::uint64_t value{0};   // DBGBVR<n>_EL1: breakpoint virtual address
    std::uint32_t control{0}; // DBGBCR<n>_EL1: control register

    // Control register field extraction helpers.
    bool enabled() const noexcept { return (control & 0x1u) != 0; }
    DebugPrivilegeMode privilege_mode() const noexcept {
        return static_cast<DebugPrivilegeMode>((control >> 1u) & 0x3u);
    }
    // BAS (Byte Address Select) field: for instruction breakpoints, always 0b1111.
    std::uint8_t bas() const noexcept {
        return static_cast<std::uint8_t>((control >> 5u) & 0xFu);
    }
    // LBN (Linked Breakpoint Number) field: bits [19:16].
    std::uint8_t linked_breakpoint_number() const noexcept {
        return static_cast<std::uint8_t>((control >> 16u) & 0xFu);
    }
    // BT (Breakpoint Type) field:
    //   0b0000 = unlinked address match
    //   0b0001 = linked address match
    //   0b0011 = linked context ID match
    std::uint8_t breakpoint_type() const noexcept {
        return static_cast<std::uint8_t>((control >> 20u) & 0xFu);
    }
    bool is_linked() const noexcept {
        return breakpoint_type() == 1u;
    }

    // Reset to architectural reset state (disabled).
    void reset() noexcept {
        value = 0;
        control = 0;
    }
};

// Watchpoint register state (DBGWVR<n>_EL1 and DBGWCR<n>_EL1).
struct WatchpointRegister {
    std::uint64_t value{0};   // DBGWVR<n>_EL1: watchpoint virtual address
    std::uint32_t control{0}; // DBGWCR<n>_EL1: control register

    // Control register field extraction helpers.
    bool enabled() const noexcept { return (control & 0x1u) != 0; }
    DebugPrivilegeMode privilege_mode() const noexcept {
        return static_cast<DebugPrivilegeMode>((control >> 1u) & 0x3u);
    }
    WatchpointLoadStoreControl load_store_control() const noexcept {
        return static_cast<WatchpointLoadStoreControl>((control >> 3u) & 0x3u);
    }
    // BAS (Byte Address Select) field: byte-granular matching within 8-byte region.
    // Bits [12:5] map to bytes [0..7] of the aligned 8-byte region.
    std::uint8_t bas() const noexcept {
        return static_cast<std::uint8_t>((control >> 5u) & 0xFFu);
    }
    // LBN (Linked Breakpoint Number) field: bits [19:16].
    std::uint8_t linked_breakpoint_number() const noexcept {
        return static_cast<std::uint8_t>((control >> 16u) & 0xFu);
    }
    // WT (Watchpoint Type) field: bit 20.
    //   0 = unlinked data address match
    //   1 = linked to breakpoint specified by LBN
    bool is_linked() const noexcept {
        return ((control >> 20u) & 0x1u) != 0;
    }
    bool watchpoint_type() const noexcept {
        return ((control >> 20u) & 0x1u) != 0;
    }
    // MASK field (bits [28:24]): address masking. Phase 51: reserved (always 0).
    std::uint8_t mask() const noexcept {
        return static_cast<std::uint8_t>((control >> 24u) & 0x1Fu);
    }

    // Reset to architectural reset state (disabled).
    void reset() noexcept {
        value = 0;
        control = 0;
    }
};

// Debug event information for reporting to the exception subsystem.
struct DebugEvent {
    DebugEventType type{DebugEventType::None};
    std::uint64_t address{0};         // Breakpoint/watchpoint address
    DebugAccessType access{DebugAccessType::Read}; // For watchpoints
    std::size_t access_size{0};       // For watchpoints (bytes)
    std::size_t slot_index{0};        // Which breakpoint/watchpoint fired
    unsigned source_el{0};            // EL that generated the event
    bool is_exclusive{false};         // Software-step exclusive marker
    std::uint32_t esr{0};             // Syndrome, when supplied by the consumer

    bool is_valid() const noexcept { return type != DebugEventType::None; }
    void clear() noexcept { type = DebugEventType::None; }
};

// Per-core debug architecture state. Each CPU instance owns exactly one
// DebugArchitecture, ensuring independent debug state across cores.
class DebugArchitecture {
public:
    DebugArchitecture() noexcept;

    // Reset all debug state to architectural reset values (all disabled).
    // Called by CPU::reset() and during PSCI lifecycle operations.
    void reset() noexcept;

    // --- Monitor Debug System Control Register (MDSCR_EL1) ---
    // Simplified subset: only the MDE (Monitor Debug Enable) bit is modeled.
    // Full MDSCR_EL1 implementation deferred to Stage 3.
    std::uint64_t get_mdscr_el1() const noexcept { return mdscr_el1_; }
    void set_mdscr_el1(std::uint64_t value) noexcept;

    // MDE (Monitor Debug Enable): bit 15. When 0, all debug exceptions are disabled.
    bool monitor_debug_enabled() const noexcept {
        return (mdscr_el1_ & (1ULL << 15u)) != 0;
    }

    // SS (Software Step): bit 0. When 1, single-step is enabled.
    bool software_step_enabled() const noexcept {
        return (mdscr_el1_ & 0x1u) != 0;
    }

    // --- Debug Communication Channel Interrupt Enable (MDCCINT_EL1) ---
    // Stage 39: bits [3:0] are modeled as guest-visible enables for the four
    // debug communication channels.  The emulator has no external DCC peer, so
    // these bits are metadata only and never generate an interrupt.  This is an
    // explicit, documented limitation.
    std::uint64_t get_mdccint_el1() const noexcept { return mdccint_el1_; }
    void set_mdccint_el1(std::uint64_t value) noexcept;
    bool dcc_interrupt_enabled(unsigned channel) const noexcept {
        return channel < 4u && (mdccint_el1_ & (1ULL << channel)) != 0u;
    }

    // --- Breakpoint Registers (DBGBVR/DBGBCR) ---
    // Read/write breakpoint value and control registers.
    // Throws std::out_of_range if index >= kDebugBreakpointCount.
    std::uint64_t get_dbgbvr(std::size_t index) const;
    void set_dbgbvr(std::size_t index, std::uint64_t value);
    std::uint32_t get_dbgbcr(std::size_t index) const;
    void set_dbgbcr(std::size_t index, std::uint32_t value);

    // --- Watchpoint Registers (DBGWVR/DBGWCR) ---
    // Read/write watchpoint value and control registers.
    // Throws std::out_of_range if index >= kDebugWatchpointCount.
    std::uint64_t get_dbgwvr(std::size_t index) const;
    void set_dbgwvr(std::size_t index, std::uint64_t value);
    std::uint32_t get_dbgwcr(std::size_t index) const;
    void set_dbgwcr(std::size_t index, std::uint32_t value);

    // --- Stage 39: Control Decoding and Support Classification ---
    // Decode a raw DBGBCR_EL1/DBGWCR_EL1 value without consulting slot context.
    // The `active` field is the authoritative "may fire" predicate.  Note that
    // linked breakpoints (BT == 1) require additional slot validation, so this
    // static decode reports them as Supported while `breakpoint_slot_active()`
    // performs the LBN checks.
    [[nodiscard]] static DebugControlDecode decode_breakpoint_control(
        std::uint32_t control) noexcept;
    [[nodiscard]] static DebugControlDecode decode_watchpoint_control(
        std::uint32_t control) noexcept;

    // Slot-level activity, including link validation and reserved-field checks.
    [[nodiscard]] bool breakpoint_slot_active(std::size_t index) const noexcept;
    [[nodiscard]] bool watchpoint_slot_active(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t active_breakpoint_count() const noexcept;
    [[nodiscard]] std::size_t active_watchpoint_count() const noexcept;

    // --- Stage 39: Debug Event Accounting ---
    // Matching records the most recent event (const-qualified because it only
    // updates observation counters).  Consumers may also record software-step
    // events explicitly via record_debug_event().
    [[nodiscard]] const DebugEvent* pending_event() const noexcept;
    [[nodiscard]] std::uint64_t debug_event_sequence() const noexcept {
        return debug_event_sequence_;
    }
    void clear_pending_event() noexcept;
    void record_debug_event(const DebugEvent& event) const noexcept;

    // --- Breakpoint Matching ---
    // Check if the given virtual address matches any enabled execution breakpoint
    // at the specified exception level. Returns a DebugEvent if a match occurs.
    // This is called AFTER PC alignment check and BEFORE instruction fetch.
    // Virtual address matching: comparison occurs before MMU translation.
    std::optional<DebugEvent> check_execution_breakpoint(
        std::uint64_t virtual_address,
        unsigned current_el) const noexcept;

    // --- Watchpoint Matching ---
    // Check if the given virtual address and access type match any enabled
    // data watchpoint at the specified exception level. Returns a DebugEvent
    // if a match occurs.
    // This is called during CheckedGuestMemory data access, AFTER alignment
    // check and BEFORE translation (virtual address matching model).
    // The caller ensures this is only called for successful (non-faulting) accesses.
    std::optional<DebugEvent> check_data_watchpoint(
        std::uint64_t virtual_address,
        std::size_t access_size,
        DebugAccessType access_type,
        unsigned current_el,
        std::uint64_t instruction_pc = 0) const noexcept;

    // --- Debug Capability Reporting (for ID_AA64DFR0_EL1) ---
    // Returns the number of breakpoints minus 1 (for ID_AA64DFR0_EL1.BRPs field).
    static constexpr std::size_t breakpoint_count_minus_one() noexcept {
        return kDebugBreakpointCount - 1;
    }
    // Returns the number of watchpoints minus 1 (for ID_AA64DFR0_EL1.WRPs field).
    static constexpr std::size_t watchpoint_count_minus_one() noexcept {
        return kDebugWatchpointCount - 1;
    }

    // --- Inspection and Debugging ---
    // Returns a human-readable summary of the current debug state.
    std::string inspect() const;

private:
    // Helper: check if the privilege mode filter allows matching at the given EL.
    static bool privilege_mode_matches(DebugPrivilegeMode mode, unsigned el) noexcept;

    // Helper: check if the watchpoint BAS field matches the access address and size.
    static bool bas_matches(std::uint8_t bas, std::uint64_t watchpoint_address,
                           std::uint64_t access_address, std::size_t access_size) noexcept;

    // Helper: evaluate whether a breakpoint condition is satisfied, supporting linking and cascades.
    bool evaluate_breakpoint_match(
        std::size_t slot_index,
        std::uint64_t virtual_address,
        unsigned current_el,
        std::uint32_t visited_mask) const noexcept;

    // Monitor Debug System Control Register (MDSCR_EL1).
    // Phase 51 Stage 2: only bit 15 (MDE) is modeled; other bits reserved.
    std::uint64_t mdscr_el1_{0};

    // Stage 39: MDCCINT_EL1 metadata (bits [3:0], no interrupt generation).
    std::uint64_t mdccint_el1_{0};

    // Four hardware execution breakpoints.
    BreakpointRegister breakpoints_[kDebugBreakpointCount]{};

    // Four hardware data watchpoints.
    WatchpointRegister watchpoints_[kDebugWatchpointCount]{};

    // Stage 39: observation of the most recent debug event.
    mutable std::optional<DebugEvent> pending_event_{};
    mutable std::uint64_t debug_event_sequence_{0};
};

} // namespace winios
