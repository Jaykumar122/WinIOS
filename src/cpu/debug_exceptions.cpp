#include "debug_exceptions.h"

namespace winios {

unsigned determine_debug_target_el(unsigned source_el,
                                    std::uint64_t mdcr_el2,
                                    std::uint64_t mdcr_el3,
                                    std::uint64_t hcr_el2) noexcept {
    // 1. Current EL is EL3 -> exceptions stay in EL3
    if (source_el >= 3u) {
        return 3u;
    }

    // 2. MDCR_EL3.TDE (bit 8): Trap Debug Exceptions to EL3
    if ((mdcr_el3 & (1ULL << 8u)) != 0u) {
        return 3u;
    }

    // 3. Current EL is EL2 -> exceptions stay in EL2 (unless trapped to EL3 above)
    if (source_el == 2u) {
        return 2u;
    }

    // 4. MDCR_EL2.TDE (bit 8): Trap Debug Exceptions to EL2
    if ((mdcr_el2 & (1ULL << 8u)) != 0u) {
        return 2u;
    }

    // 5. Source is EL0 and HCR_EL2.TGE (bit 27) is set -> Trap General Exceptions to EL2
    if (source_el == 0u && (hcr_el2 & (1ULL << 27u)) != 0u) {
        return 2u;
    }

    // 6. Default routing for guest execution -> EL1
    return 1u;
}

std::uint32_t determine_debug_vector_offset(unsigned source_el,
                                             unsigned target_el,
                                             bool sp_sel) noexcept {
    if (source_el < target_el) {
        // Exception taken from lower Exception Level in AArch64
        return 0x400u;
    }
    // Exception taken from same Exception Level
    return sp_sel ? 0x200u : 0x000u;
}

SynchronousException create_debug_synchronous_exception(
    const DebugExceptionEvent& event,
    unsigned target_el) {
    const bool same_el = (target_el == event.source_el);
    SynchronousException ex;
    ex.target_el = target_el;
    ex.same_el = same_el;
    ex.preferred_return = event.instruction_address;

    switch (event.kind) {
    case DebugExceptionKind::HardwareBreakpoint:
        // Armv8-A DDI 0487: EC 0x30 for lower EL, EC 0x31 for same EL.
        // ISS is RES0 (0). IL is 1. FAR is UNKNOWN.
        ex.ec = same_el ? ExceptionClass::BreakpointSame : ExceptionClass::BreakpointLower;
        ex.iss = 0u;
        ex.has_fault_address = false;
        ex.fault_address = 0;
        break;

    case DebugExceptionKind::HardwareWatchpoint:
        // Armv8-A DDI 0487: EC 0x34 for lower EL, EC 0x35 for same EL.
        // ISS: DFSC = 0b100010 (0x22 Synchronous Watchpoint).
        //      WnR (bit 6) = 1 for Write/Store, 0 for Read/Load.
        //      IL is 1. FAR holds faulting data virtual address.
        ex.ec = same_el ? ExceptionClass::WatchpointSame : ExceptionClass::WatchpointLower;
        ex.iss = (event.is_write ? (1u << 6u) : 0u) | 0x22u;
        ex.has_fault_address = true;
        ex.fault_address = event.fault_address;
        ex.access = event.is_write ? AccessType::Write : AccessType::Read;
        break;

    case DebugExceptionKind::SoftwareBreakpoint:
        // Future-compatible BRK instruction (EC 0x3C in AArch64)
        // ISS holds imm16 comment payload
        ex.ec = static_cast<ExceptionClass>(0x3C);
        ex.iss = static_cast<std::uint32_t>(event.comment);
        ex.has_fault_address = false;
        ex.fault_address = 0;
        break;

    case DebugExceptionKind::SoftwareStep:
        // Armv8-A DDI 0487: EC 0x32 for lower EL, EC 0x33 for same EL.
        // ISS: EX (bit 6) set if stepped instruction was exclusive.
        //      DFSC = 0b100010 (0x22 Synchronous Debug Exception).
        //      IL is 1. FAR is UNKNOWN.
        ex.ec = same_el ? ExceptionClass::SoftwareStepSame : ExceptionClass::SoftwareStepLower;
        ex.iss = (event.is_exclusive ? (1u << 6u) : 0u) | 0x22u;
        ex.has_fault_address = false;
        ex.fault_address = 0;
        break;
    }

    return ex;
}

} // namespace winios
