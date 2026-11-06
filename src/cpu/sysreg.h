#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

class CPU;

namespace winios::sysreg {

// -----------------------------------------------------------------------------
// System Register Identifiers
// -----------------------------------------------------------------------------
enum class SysRegId : std::uint16_t {
    // CurrentEL & PSTATE
    CurrentEL,
    DAIF,
    NZCV,
    SPSel,
    FPCR,
    FPSR,

    // Exception Architecture EL1
    SPSR_EL1,
    ELR_EL1,
    ESR_EL1,
    FAR_EL1,
    VBAR_EL1,
    SP_EL0,

    // Thread Pointer Registers
    TPIDR_EL0,
    TPIDR_EL1,
    TPIDRRO_EL0,

    // MMU / Virtual Memory System Registers EL1
    SCTLR_EL1,
    CPACR_EL1,
    TCR_EL1,
    TTBR0_EL1,
    TTBR1_EL1,
    MAIR_EL1,
    PAR_EL1,
    ID_AA64MMFR0_EL1,
    DCZID_EL0,
    MPIDR_EL1,

    // Generic Timers
    CNTFRQ_EL0,
    CNTPCT_EL0,
    CNTVCT_EL0,
    CNTKCTL_EL1,
    CNTP_CTL_EL0,
    CNTP_CVAL_EL0,
    CNTP_TVAL_EL0,
    CNTV_CTL_EL0,
    CNTV_CVAL_EL0,
    CNTV_TVAL_EL0,

    // EL2 Virtualization System Registers
    SP_EL1,
    SP_EL2,
    SPSR_EL2,
    ELR_EL2,
    ESR_EL2,
    FAR_EL2,
    HPFAR_EL2,
    VBAR_EL2,
    HCR_EL2,
    VTTBR_EL2,
    VTCR_EL2,
    CNTVOFF_EL2,
    CNTHCTL_EL2,
    CNTHP_CTL_EL2,
    CNTHP_CVAL_EL2,
    CNTHP_TVAL_EL2,
    MDCR_EL2,

    // EL3 Secure Monitor System Registers
    SP_EL3,
    SPSR_EL3,
    ELR_EL3,
    ESR_EL3,
    FAR_EL3,
    VBAR_EL3,
    SCR_EL3,
    CPTR_EL3,
    MDCR_EL3,

    // GICv3 System Registers
    ICC_PMR_EL1,
    ICC_IAR0_EL1,
    ICC_EOIR0_EL1,
    ICC_HPPIR0_EL1,
    ICC_BPR0_EL1,
    ICC_IAR1_EL1,
    ICC_EOIR1_EL1,
    ICC_HPPIR1_EL1,
    ICC_BPR1_EL1,
    ICC_SRE_EL1,
    ICC_IGRPEN0_EL1,
    ICC_IGRPEN1_EL1,
    ICC_DIR_EL1,
    ICC_RPR_EL1,
    ICC_CTLR_EL1,
    ICC_SRE_EL2,
    ICC_SGI1R_EL1,
    ICH_HCR_EL2,
    ICH_VTR_EL2,
    ICH_MISR_EL2,
    ICH_EISR_EL2,
    ICH_ELRSR_EL2,
    ICH_VMCR_EL2,
    ICH_LR0_EL2,
    ICH_LR1_EL2,
    ICH_LR2_EL2,
    ICH_LR3_EL2,

    // PMU (Performance Monitoring Unit)
    PMCR_EL0,
    PMCNTENSET_EL0,
    PMCNTENCLR_EL0,
    PMOVSCLR_EL0,
    PMSWINC_EL0,
    PMSELR_EL0,
    PMCEID0_EL0,
    PMCEID1_EL0,
    PMCCNTR_EL0,
    PMXEVTYPER_EL0,
    PMXEVCNTR_EL0,
    PMUSERENR_EL0,
    PMINTENSET_EL1,
    PMINTENCLR_EL1,
    PMOVSSET_EL0,
    PMCCFILTR_EL0,

    // Debug Architecture Registers
    MDSCR_EL1,
    DBGBVR0_EL1, DBGBVR1_EL1, DBGBVR2_EL1, DBGBVR3_EL1,
    DBGBCR0_EL1, DBGBCR1_EL1, DBGBCR2_EL1, DBGBCR3_EL1,
    DBGWVR0_EL1, DBGWVR1_EL1, DBGWVR2_EL1, DBGWVR3_EL1,
    DBGWCR0_EL1, DBGWCR1_EL1, DBGWCR2_EL1, DBGWCR3_EL1,
    ID_AA64DFR0_EL1
};

// -----------------------------------------------------------------------------
// Well-known sysreg keys
// Key format: (o0 << 14) | (op1 << 11) | (CRn << 7) | (CRm << 3) | op2
// where o0 = (op0 == 2) ? 0 : 1
// -----------------------------------------------------------------------------
constexpr std::uint16_t make_sysreg_key(std::uint8_t op0, std::uint8_t op1,
                                        std::uint8_t crn, std::uint8_t crm,
                                        std::uint8_t op2) noexcept {
    const std::uint32_t o0 = (op0 == 2) ? 0u : 1u;
    return static_cast<std::uint16_t>(
        (o0 << 14u) | ((op1 & 0x7u) << 11u) | ((crn & 0xFu) << 7u) |
        ((crm & 0xFu) << 3u) | (op2 & 0x7u));
}

// -----------------------------------------------------------------------------
// System Register Descriptor
// -----------------------------------------------------------------------------
struct SysRegDescriptor {
    SysRegId id;
    const char* name;
    std::uint16_t key;
    std::uint8_t op0; // 2 or 3
    std::uint8_t op1; // 0..7
    std::uint8_t crn; // 0..15
    std::uint8_t crm; // 0..15
    std::uint8_t op2; // 0..7
    unsigned min_el;  // Minimum architectural EL (0, 1, 2, or 3)
    bool is_readable;
    bool is_writable;
    std::uint8_t width; // 32 or 64
    std::uint64_t reset_value;
    std::uint64_t res0_mask;
    std::uint64_t res1_mask;
};

// -----------------------------------------------------------------------------
// Privilege Checking
// -----------------------------------------------------------------------------
enum class PrivilegeResult : std::uint8_t {
    Allowed,
    UndefinedException, // Inaccessible at current EL (generates EC=0x00)
    TrapToEl2,          // Trapped to EL2 (EC=0x18)
    TrapToEl3           // Trapped to EL3 (EC=0x18)
};

// Lookup functions
[[nodiscard]] const SysRegDescriptor* find_descriptor_by_key(std::uint16_t key) noexcept;
[[nodiscard]] const SysRegDescriptor* find_descriptor_by_name(std::string_view name) noexcept;
[[nodiscard]] const SysRegDescriptor* find_descriptor_by_id(SysRegId id) noexcept;
[[nodiscard]] std::span<const SysRegDescriptor> get_all_descriptors() noexcept;

// Centralized privilege checker
[[nodiscard]] PrivilegeResult check_privilege(const SysRegDescriptor& desc,
                                             unsigned current_el,
                                             bool is_read,
                                             const CPU& cpu) noexcept;

} // namespace winios::sysreg
