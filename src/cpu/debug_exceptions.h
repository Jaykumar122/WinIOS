#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include "synchronous_exception.h"

// Phase 51 Stage 6: Consolidated AArch64 Debug Exception Subsystem
//
// Unifies exception classification, target EL resolution, vector offset calculation,
// syndrome generation, and register preservation across hardware execution breakpoints
// and hardware data watchpoints, with future-compatible software breakpoint definitions.

namespace winios {

enum class DebugExceptionKind : std::uint32_t {
    HardwareBreakpoint = 0,
    HardwareWatchpoint = 1,
    SoftwareBreakpoint = 2, // Future-compatible BRK instruction
    SoftwareStep       = 3, // Future-compatible (Stage 8)
};

// Information captured at the moment a debug event is detected.
struct DebugExceptionEvent {
    DebugExceptionKind kind{DebugExceptionKind::HardwareBreakpoint};
    unsigned source_el{0};
    std::uint64_t instruction_address{0};
    std::uint64_t fault_address{0}; // Valid for watchpoints
    bool is_write{false};            // Valid for watchpoints
    std::size_t slot_index{0};       // 0..3 for HW BP/WP
    std::uint16_t comment{0};        // For future BRK imm16
    bool is_exclusive{false};        // Valid for software step (EX bit in ISS)
};

// Computes target Exception Level based on Armv8-A DDI 0487 rules:
// - If source_el is 3, stays at EL3.
// - If MDCR_EL3.TDE (bit 8) is set, traps to EL3.
// - If source_el is 2, stays at EL2 (unless routed to EL3).
// - If MDCR_EL2.TDE (bit 8) is set, traps to EL2.
// - If source_el is 0 and HCR_EL2.TGE (bit 27) is set, traps to EL2.
// - Otherwise, targets EL1.
unsigned determine_debug_target_el(unsigned source_el,
                                    std::uint64_t mdcr_el2,
                                    std::uint64_t mdcr_el3,
                                    std::uint64_t hcr_el2 = 0) noexcept;

// Computes the vector table offset (0x000, 0x200, 0x400) given source EL,
// target EL, and SPSel.
std::uint32_t determine_debug_vector_offset(unsigned source_el,
                                             unsigned target_el,
                                             bool sp_sel) noexcept;

// Builds the SynchronousException for a debug event and chosen target EL.
SynchronousException create_debug_synchronous_exception(
    const DebugExceptionEvent& event,
    unsigned target_el);

} // namespace winios
