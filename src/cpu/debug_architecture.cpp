#include "cpu/debug_architecture.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace winios {

DebugArchitecture::DebugArchitecture() noexcept {
    reset();
}

void DebugArchitecture::reset() noexcept {
    mdscr_el1_ = 0;
    for (auto& bp : breakpoints_) {
        bp.reset();
    }
    for (auto& wp : watchpoints_) {
        wp.reset();
    }
}

void DebugArchitecture::set_mdscr_el1(std::uint64_t value) noexcept {
    // Stage 8: SS (bit 0) and MDE (bit 15) are supported. Other bits are reserved
    // and read-as-zero. We silently mask unsupported bits rather than faulting.
    constexpr std::uint64_t kSupportedMask = (1ULL << 15u) | (1ULL << 0u);
    mdscr_el1_ = value & kSupportedMask;
}

// --- Breakpoint Register Accessors ---

std::uint64_t DebugArchitecture::get_dbgbvr(std::size_t index) const {
    if (index >= kDebugBreakpointCount) {
        throw std::out_of_range("Breakpoint index out of range");
    }
    return breakpoints_[index].value;
}

void DebugArchitecture::set_dbgbvr(std::size_t index, std::uint64_t value) {
    if (index >= kDebugBreakpointCount) {
        throw std::out_of_range("Breakpoint index out of range");
    }
    breakpoints_[index].value = value;
}

std::uint32_t DebugArchitecture::get_dbgbcr(std::size_t index) const {
    if (index >= kDebugBreakpointCount) {
        throw std::out_of_range("Breakpoint index out of range");
    }
    return breakpoints_[index].control;
}

void DebugArchitecture::set_dbgbcr(std::size_t index, std::uint32_t value) {
    if (index >= kDebugBreakpointCount) {
        throw std::out_of_range("Breakpoint index out of range");
    }
    // Phase 51: validate and mask supported fields.
    // Supported: E (bit 0), PMC (bits [2:1]), BAS (bits [8:5]), BT (bits [23:20]).
    // Reserved/unsupported: HMC, SSC, LBN, MASK and other fields.
    // We allow all bit patterns but only honor the supported fields during matching.
    // BT must be 0b0000 (unlinked address match); other values are reserved but accepted.
    breakpoints_[index].control = value;
}

// --- Watchpoint Register Accessors ---

std::uint64_t DebugArchitecture::get_dbgwvr(std::size_t index) const {
    if (index >= kDebugWatchpointCount) {
        throw std::out_of_range("Watchpoint index out of range");
    }
    return watchpoints_[index].value;
}

void DebugArchitecture::set_dbgwvr(std::size_t index, std::uint64_t value) {
    if (index >= kDebugWatchpointCount) {
        throw std::out_of_range("Watchpoint index out of range");
    }
    watchpoints_[index].value = value;
}

std::uint32_t DebugArchitecture::get_dbgwcr(std::size_t index) const {
    if (index >= kDebugWatchpointCount) {
        throw std::out_of_range("Watchpoint index out of range");
    }
    return watchpoints_[index].control;
}

void DebugArchitecture::set_dbgwcr(std::size_t index, std::uint32_t value) {
    if (index >= kDebugWatchpointCount) {
        throw std::out_of_range("Watchpoint index out of range");
    }
    // Phase 51: validate and mask supported fields.
    // Supported: E (bit 0), PAC (bits [2:1]), LSC (bits [4:3]), BAS (bits [12:5]).
    // Reserved/unsupported: HMC, SSC, LBN, WT, MASK and other fields.
    // We allow all bit patterns but only honor the supported fields during matching.
    watchpoints_[index].control = value;
}

// --- Privilege Mode Filtering ---

bool DebugArchitecture::privilege_mode_matches(DebugPrivilegeMode mode,
                                                unsigned el) noexcept {
    switch (mode) {
    case DebugPrivilegeMode::None:
        // No filtering: matches all ELs.
        return true;
    case DebugPrivilegeMode::EL1:
        return el == 1;
    case DebugPrivilegeMode::EL0:
        return el == 0;
    case DebugPrivilegeMode::EL0_EL1:
        return el == 0 || el == 1;
    default:
        // Unknown mode: treat as no match (defensive).
        return false;
    }
}

// --- Breakpoint Matching ---

bool DebugArchitecture::evaluate_breakpoint_match(
    std::size_t slot_index,
    std::uint64_t virtual_address,
    unsigned current_el,
    std::uint32_t visited_mask) const noexcept {
    if (slot_index >= kDebugBreakpointCount) {
        return false;
    }

    // Cycle detection for linked cascades.
    if ((visited_mask & (1u << slot_index)) != 0) {
        return false;
    }
    visited_mask |= (1u << slot_index);

    const auto& bp = breakpoints_[slot_index];
    if (!bp.enabled()) {
        return false;
    }

    if (!privilege_mode_matches(bp.privilege_mode(), current_el)) {
        return false;
    }

    const auto bt = bp.breakpoint_type();
    switch (bt) {
    case 0: // Unlinked instruction address match
        return bp.value == virtual_address;

    case 1: { // Linked instruction address match
        if (bp.value != virtual_address) {
            return false;
        }
        const auto lbn = bp.linked_breakpoint_number();
        if (lbn >= kDebugBreakpointCount || lbn == slot_index) {
            // Unimplemented slot or self-referential link is invalid -> does not match
            return false;
        }
        return evaluate_breakpoint_match(lbn, virtual_address, current_el, visited_mask);
    }

    case 3: // Linked Context ID match / secondary link target
        return (bp.value == virtual_address || bp.value == 0);

    default:
        // Reserved/unsupported BT encoding
        return false;
    }
}

std::optional<DebugEvent> DebugArchitecture::check_execution_breakpoint(
    std::uint64_t virtual_address,
    unsigned current_el) const noexcept {
    // If monitor debug is disabled, no breakpoints fire.
    if (!monitor_debug_enabled()) {
        return std::nullopt;
    }

    // Check each breakpoint in order; return the first match.
    // ARM architecture does not specify priority among multiple matches; we
    // use slot order as the tiebreaker (slot 0 has highest priority).
    for (std::size_t i = 0; i < kDebugBreakpointCount; ++i) {
        const auto& bp = breakpoints_[i];

        // Skip disabled breakpoints.
        if (!bp.enabled()) {
            continue;
        }

        // BT 3 (Linked Context ID match) is a secondary target only; it never fires
        // directly as a top-level primary breakpoint.
        if (bp.breakpoint_type() != 0 && bp.breakpoint_type() != 1) {
            continue;
        }

        if (evaluate_breakpoint_match(i, virtual_address, current_el, 0)) {
            DebugEvent event;
            event.type = DebugEventType::Breakpoint;
            event.address = virtual_address;
            event.slot_index = i;
            return event;
        }
    }

    // No match.
    return std::nullopt;
}

// --- Byte Address Select (BAS) Matching ---

bool DebugArchitecture::bas_matches(std::uint8_t bas,
                                    std::uint64_t watchpoint_address,
                                    std::uint64_t access_address,
                                    std::size_t access_size) noexcept {
    // BAS (Byte Address Select) allows byte-granular watchpoint matching within
    // an 8-byte aligned region. Each bit in BAS corresponds to one byte:
    //   BAS[0] = byte 0 (address & ~0x7 + 0)
    //   BAS[1] = byte 1 (address & ~0x7 + 1)
    //   ...
    //   BAS[7] = byte 7 (address & ~0x7 + 7)
    //
    // The watchpoint matches if ANY byte of the access falls within a byte
    // selected by BAS.

    // Align the watchpoint address to 8-byte boundary.
    const std::uint64_t watchpoint_base = watchpoint_address & ~0x7ULL;

    // Compute the range of bytes accessed: [access_start, access_end).
    const std::uint64_t access_start = access_address;
    const std::uint64_t access_end = access_address + access_size;

    // Check each byte of the access to see if it falls within a BAS-selected byte.
    for (std::uint64_t byte_addr = access_start; byte_addr < access_end; ++byte_addr) {
        // Compute the aligned 8-byte region for this access byte.
        const std::uint64_t byte_base = byte_addr & ~0x7ULL;

        // If this byte is not in the same 8-byte region as the watchpoint, skip.
        if (byte_base != watchpoint_base) {
            continue;
        }

        // Compute the bit index within BAS (0..7).
        const std::uint64_t byte_offset = byte_addr & 0x7ULL;
        const std::uint8_t byte_bit = 1u << byte_offset;

        // Check if this byte is selected by BAS.
        if ((bas & byte_bit) != 0) {
            return true;
        }
    }

    // No overlap with any BAS-selected byte.
    return false;
}

// --- Watchpoint Matching ---

std::optional<DebugEvent> DebugArchitecture::check_data_watchpoint(
    std::uint64_t virtual_address,
    std::size_t access_size,
    DebugAccessType access_type,
    unsigned current_el,
    std::uint64_t instruction_pc) const noexcept {
    // If monitor debug is disabled, no watchpoints fire.
    if (!monitor_debug_enabled()) {
        return std::nullopt;
    }

    // Check each watchpoint in order; return the first match.
    for (std::size_t i = 0; i < kDebugWatchpointCount; ++i) {
        const auto& wp = watchpoints_[i];

        // Skip disabled watchpoints.
        if (!wp.enabled()) {
            continue;
        }

        // Check load/store control filter.
        const auto lsc = wp.load_store_control();
        if (lsc == WatchpointLoadStoreControl::Neither) {
            // Reserved value; treat as disabled.
            continue;
        }
        if (lsc == WatchpointLoadStoreControl::Load && access_type != DebugAccessType::Read) {
            continue;
        }
        if (lsc == WatchpointLoadStoreControl::Store && access_type != DebugAccessType::Write) {
            continue;
        }
        // WatchpointLoadStoreControl::Both matches both read and write.

        // Check privilege mode filter.
        if (!privilege_mode_matches(wp.privilege_mode(), current_el)) {
            continue;
        }

        // Phase 51: MASK field is reserved (always 0). We do not support address
        // masking beyond BAS. If guest sets MASK != 0, we ignore it and use
        // only BAS matching.

        // Byte Address Select (BAS) matching.
        if (!bas_matches(wp.bas(), wp.value, virtual_address, access_size)) {
            continue;
        }

        // Stage 7: Linked watchpoint matching (WT == 1).
        if (wp.is_linked()) {
            const auto lbn = wp.linked_breakpoint_number();
            if (lbn >= kDebugBreakpointCount) {
                // Link to unimplemented breakpoint slot -> treated as disabled
                continue;
            }

            const auto& bp = breakpoints_[lbn];
            if (!bp.enabled()) {
                // Linked breakpoint disabled -> watchpoint does not trigger
                continue;
            }

            if (!privilege_mode_matches(bp.privilege_mode(), current_el)) {
                // Privilege mode mismatch on linked breakpoint
                continue;
            }

            // Linked breakpoint address match against instruction PC:
            // The instruction executing the memory access must match DBGBVR[lbn].
            if (bp.value != instruction_pc) {
                continue;
            }

            // If the linked breakpoint is itself linked in cascade, check its cascade:
            if (bp.is_linked()) {
                const auto cascade_lbn = bp.linked_breakpoint_number();
                if (cascade_lbn >= kDebugBreakpointCount || cascade_lbn == lbn) {
                    continue;
                }
                if (!evaluate_breakpoint_match(cascade_lbn, instruction_pc, current_el, (1u << lbn))) {
                    continue;
                }
            }
        }

        // Match found: report watchpoint debug event.
        DebugEvent event;
        event.type = DebugEventType::Watchpoint;
        event.address = virtual_address;
        event.access = access_type;
        event.access_size = access_size;
        event.slot_index = i;
        return event;
    }

    // No match.
    return std::nullopt;
}

// --- Inspection ---

std::string DebugArchitecture::inspect() const {
    std::ostringstream stream;
    stream << "Debug Architecture State:\n";
    stream << "  MDSCR_EL1: 0x" << std::hex << mdscr_el1_ << std::dec;
    stream << " (MDE=" << (monitor_debug_enabled() ? "enabled" : "disabled")
           << ", SS=" << (software_step_enabled() ? "enabled" : "disabled") << ")\n";

    stream << "  Breakpoints:\n";
    for (std::size_t i = 0; i < kDebugBreakpointCount; ++i) {
        const auto& bp = breakpoints_[i];
        stream << "    [" << i << "] DBGBVR=0x" << std::hex << bp.value
               << " DBGBCR=0x" << bp.control << std::dec;
        if (bp.enabled()) {
            stream << " (enabled, PMC=" << static_cast<unsigned>(bp.privilege_mode())
                   << ", BT=" << static_cast<unsigned>(bp.breakpoint_type());
            if (bp.is_linked()) {
                stream << ", LBN=" << static_cast<unsigned>(bp.linked_breakpoint_number());
            }
            stream << ")";
        } else {
            stream << " (disabled)";
        }
        stream << "\n";
    }

    stream << "  Watchpoints:\n";
    for (std::size_t i = 0; i < kDebugWatchpointCount; ++i) {
        const auto& wp = watchpoints_[i];
        stream << "    [" << i << "] DBGWVR=0x" << std::hex << wp.value
               << " DBGWCR=0x" << wp.control << std::dec;
        if (wp.enabled()) {
            stream << " (enabled, PAC=" << static_cast<unsigned>(wp.privilege_mode())
                   << ", LSC=" << static_cast<unsigned>(wp.load_store_control())
                   << ", BAS=0x" << std::hex << static_cast<unsigned>(wp.bas()) << std::dec;
            if (wp.is_linked()) {
                stream << ", WT=1, LBN=" << static_cast<unsigned>(wp.linked_breakpoint_number());
            } else {
                stream << ", WT=0";
            }
            stream << ")";
        } else {
            stream << " (disabled)";
        }
        stream << "\n";
    }

    return stream.str();
}

} // namespace winios
