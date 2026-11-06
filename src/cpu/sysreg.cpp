#include "cpu/sysreg.h"
#include "cpu/cpu.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace winios::sysreg {

namespace {

// Definition of all implemented system registers in WinIOS
constexpr SysRegDescriptor kDescriptors[] = {
    // --- CurrentEL & PSTATE ---
    { SysRegId::CurrentEL, "CurrentEL", make_sysreg_key(3, 0, 4, 2, 2), 3, 0, 4, 2, 2, 0, true, false, 64, 0x4, 0xFFFFFFFFFFFFFFF3ULL, 0 },
    { SysRegId::DAIF,      "DAIF",      make_sysreg_key(3, 3, 4, 2, 1), 3, 3, 4, 2, 1, 0, true, true,  64, 0x3C0, 0xFFFFFFFFFFFFFC3FULL, 0 },
    { SysRegId::NZCV,      "NZCV",      make_sysreg_key(3, 3, 4, 2, 0), 3, 3, 4, 2, 0, 0, true, true,  64, 0, 0x0FFFFFFFFFFFFFFFULL, 0 },
    { SysRegId::SPSel,     "SPSel",     make_sysreg_key(3, 0, 4, 2, 0), 3, 0, 4, 2, 0, 1, true, true,  64, 1, 0xFFFFFFFFFFFFFFFEULL, 0 },
    { SysRegId::FPCR,      "FPCR",      make_sysreg_key(3, 3, 4, 4, 0), 3, 3, 4, 4, 0, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::FPSR,      "FPSR",      make_sysreg_key(3, 3, 4, 4, 1), 3, 3, 4, 4, 1, 0, true, true,  64, 0, 0, 0 },

    // --- Exception Architecture EL1 ---
    { SysRegId::SPSR_EL1,  "SPSR_EL1",  make_sysreg_key(3, 0, 4, 0, 0), 3, 0, 4, 0, 0, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::ELR_EL1,   "ELR_EL1",   make_sysreg_key(3, 0, 4, 0, 1), 3, 0, 4, 0, 1, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::ESR_EL1,   "ESR_EL1",   make_sysreg_key(3, 0, 5, 2, 0), 3, 0, 5, 2, 0, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::FAR_EL1,   "FAR_EL1",   make_sysreg_key(3, 0, 6, 0, 0), 3, 0, 6, 0, 0, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::VBAR_EL1,  "VBAR_EL1",  make_sysreg_key(3, 0, 12, 0, 0), 3, 0, 12, 0, 0, 1, true, true, 64, 0, 0x7FFULL, 0 },
    { SysRegId::SP_EL0,    "SP_EL0",    make_sysreg_key(3, 0, 4, 1, 0), 3, 0, 4, 1, 0, 1, true, true,  64, 0, 0, 0 },

    // --- Thread Pointer Registers ---
    { SysRegId::TPIDR_EL0,   "TPIDR_EL0",   make_sysreg_key(3, 3, 13, 0, 2), 3, 3, 13, 0, 2, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::TPIDR_EL1,   "TPIDR_EL1",   make_sysreg_key(3, 0, 13, 0, 4), 3, 0, 13, 0, 4, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::TPIDRRO_EL0, "TPIDRRO_EL0", make_sysreg_key(3, 3, 13, 0, 3), 3, 3, 13, 0, 3, 0, true, true,  64, 0, 0, 0 },

    // --- MMU / Virtual Memory System Registers EL1 ---
    { SysRegId::SCTLR_EL1,        "SCTLR_EL1",        make_sysreg_key(3, 0, 1, 0, 0),  3, 0, 1, 0, 0, 1, true, true,  64, 0x00C50838ULL, 0, 0 },
    { SysRegId::CPACR_EL1,        "CPACR_EL1",        make_sysreg_key(3, 0, 1, 0, 2),  3, 0, 1, 0, 2, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::TCR_EL1,          "TCR_EL1",          make_sysreg_key(3, 0, 2, 0, 2),  3, 0, 2, 0, 2, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::TTBR0_EL1,        "TTBR0_EL1",        make_sysreg_key(3, 0, 2, 0, 0),  3, 0, 2, 0, 0, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::TTBR1_EL1,        "TTBR1_EL1",        make_sysreg_key(3, 0, 2, 0, 1),  3, 0, 2, 0, 1, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::MAIR_EL1,         "MAIR_EL1",         make_sysreg_key(3, 0, 10, 2, 0), 3, 0, 10, 2, 0, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::PAR_EL1,          "PAR_EL1",          make_sysreg_key(3, 0, 7, 4, 0),  3, 0, 7, 4, 0, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::ID_AA64MMFR0_EL1, "ID_AA64MMFR0_EL1", make_sysreg_key(3, 0, 0, 7, 0),  3, 0, 0, 7, 0, 1, true, false, 64, 0x0000000000001122ULL, 0, 0 },
    { SysRegId::DCZID_EL0,        "DCZID_EL0",        make_sysreg_key(3, 3, 0, 0, 7),  3, 3, 0, 0, 7, 0, true, false, 64, 0x4ULL, 0, 0 },
    { SysRegId::MPIDR_EL1,        "MPIDR_EL1",        make_sysreg_key(3, 0, 0, 0, 5),  3, 0, 0, 0, 5, 1, true, false, 64, 0x80000000ULL, 0, 0 },

    // --- Generic Timers ---
    { SysRegId::CNTFRQ_EL0,   "CNTFRQ_EL0",   make_sysreg_key(3, 3, 14, 0, 0), 3, 3, 14, 0, 0, 0, true, true,  64, 62500000ULL, 0, 0 },
    { SysRegId::CNTPCT_EL0,   "CNTPCT_EL0",   make_sysreg_key(3, 3, 14, 0, 1), 3, 3, 14, 0, 1, 0, true, false, 64, 0, 0, 0 },
    { SysRegId::CNTVCT_EL0,   "CNTVCT_EL0",   make_sysreg_key(3, 3, 14, 0, 2), 3, 3, 14, 0, 2, 0, true, false, 64, 0, 0, 0 },
    { SysRegId::CNTKCTL_EL1,  "CNTKCTL_EL1",  make_sysreg_key(3, 0, 14, 1, 0), 3, 0, 14, 1, 0, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::CNTP_CTL_EL0, "CNTP_CTL_EL0", make_sysreg_key(3, 3, 14, 2, 1), 3, 3, 14, 2, 1, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::CNTP_CVAL_EL0,"CNTP_CVAL_EL0",make_sysreg_key(3, 3, 14, 2, 2), 3, 3, 14, 2, 2, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::CNTP_TVAL_EL0,"CNTP_TVAL_EL0",make_sysreg_key(3, 3, 14, 2, 0), 3, 3, 14, 2, 0, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::CNTV_CTL_EL0, "CNTV_CTL_EL0", make_sysreg_key(3, 3, 14, 3, 1), 3, 3, 14, 3, 1, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::CNTV_CVAL_EL0,"CNTV_CVAL_EL0",make_sysreg_key(3, 3, 14, 3, 2), 3, 3, 14, 3, 2, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::CNTV_TVAL_EL0,"CNTV_TVAL_EL0",make_sysreg_key(3, 3, 14, 3, 0), 3, 3, 14, 3, 0, 0, true, true,  64, 0, 0, 0 },

    // --- EL2 Virtualization System Registers ---
    { SysRegId::SP_EL1,        "SP_EL1",        make_sysreg_key(3, 4, 4, 1, 0),  3, 4, 4, 1, 0, 2, true, true,  64, 0, 0, 0 },
    { SysRegId::SP_EL2,        "SP_EL2",        make_sysreg_key(3, 6, 4, 1, 0),  3, 6, 4, 1, 0, 2, true, true,  64, 0, 0, 0 },
    { SysRegId::SPSR_EL2,      "SPSR_EL2",      make_sysreg_key(3, 4, 4, 0, 0),  3, 4, 4, 0, 0, 2, true, true,  64, 0, 0, 0 },
    { SysRegId::ELR_EL2,       "ELR_EL2",       make_sysreg_key(3, 4, 4, 0, 1),  3, 4, 4, 0, 1, 2, true, true,  64, 0, 0, 0 },
    { SysRegId::ESR_EL2,       "ESR_EL2",       make_sysreg_key(3, 4, 5, 2, 0),  3, 4, 5, 2, 0, 2, true, true,  64, 0, 0, 0 },
    { SysRegId::FAR_EL2,       "FAR_EL2",       make_sysreg_key(3, 4, 6, 0, 0),  3, 4, 6, 0, 0, 2, true, true,  64, 0, 0, 0 },
    { SysRegId::HPFAR_EL2,     "HPFAR_EL2",     make_sysreg_key(3, 4, 6, 0, 4),  3, 4, 6, 0, 4, 2, true, true,  64, 0, 0, 0 },
    { SysRegId::VBAR_EL2,      "VBAR_EL2",      make_sysreg_key(3, 4, 12, 0, 0), 3, 4, 12, 0, 0, 2, true, true, 64, 0, 0, 0 },
    { SysRegId::HCR_EL2,       "HCR_EL2",       make_sysreg_key(3, 4, 1, 1, 0),  3, 4, 1, 1, 0, 2, true, true,  64, 0, 0, 0 },
    { SysRegId::VTTBR_EL2,     "VTTBR_EL2",     make_sysreg_key(3, 4, 2, 1, 0),  3, 4, 2, 1, 0, 2, true, true,  64, 0, 0, 0 },
    { SysRegId::VTCR_EL2,      "VTCR_EL2",      make_sysreg_key(3, 4, 2, 1, 2),  3, 4, 2, 1, 2, 2, true, true,  64, 0, 0, 0 },
    { SysRegId::CNTVOFF_EL2,   "CNTVOFF_EL2",   make_sysreg_key(3, 4, 14, 0, 3), 3, 4, 14, 0, 3, 2, true, true, 64, 0, 0, 0 },
    { SysRegId::CNTHCTL_EL2,   "CNTHCTL_EL2",   make_sysreg_key(3, 4, 14, 1, 0), 3, 4, 14, 1, 0, 2, true, true, 64, 0, 0, 0 },
    { SysRegId::CNTHP_CTL_EL2, "CNTHP_CTL_EL2", make_sysreg_key(3, 4, 14, 2, 1), 3, 4, 14, 2, 1, 2, true, true, 64, 0, 0, 0 },
    { SysRegId::CNTHP_CVAL_EL2,"CNTHP_CVAL_EL2",make_sysreg_key(3, 4, 14, 2, 2), 3, 4, 14, 2, 2, 2, true, true, 64, 0, 0, 0 },
    { SysRegId::CNTHP_TVAL_EL2,"CNTHP_TVAL_EL2",make_sysreg_key(3, 4, 14, 2, 0), 3, 4, 14, 2, 0, 2, true, true, 64, 0, 0, 0 },
    { SysRegId::MDCR_EL2,      "MDCR_EL2",      make_sysreg_key(3, 4, 1, 1, 1),  3, 4, 1, 1, 1, 2, true, true,  64, 0, 0, 0 },

    // --- EL3 Secure Monitor System Registers ---
    { SysRegId::SP_EL3,   "SP_EL3",   make_sysreg_key(3, 6, 4, 1, 0),  3, 6, 4, 1, 0, 3, true, true,  64, 0, 0, 0 },
    { SysRegId::SPSR_EL3, "SPSR_EL3", make_sysreg_key(3, 6, 4, 0, 0),  3, 6, 4, 0, 0, 3, true, true,  64, 0, 0, 0 },
    { SysRegId::ELR_EL3,  "ELR_EL3",  make_sysreg_key(3, 6, 4, 0, 1),  3, 6, 4, 0, 1, 3, true, true,  64, 0, 0, 0 },
    { SysRegId::ESR_EL3,  "ESR_EL3",  make_sysreg_key(3, 6, 5, 2, 0),  3, 6, 5, 2, 0, 3, true, true,  64, 0, 0, 0 },
    { SysRegId::FAR_EL3,  "FAR_EL3",  make_sysreg_key(3, 6, 6, 0, 0),  3, 6, 6, 0, 0, 3, true, true,  64, 0, 0, 0 },
    { SysRegId::VBAR_EL3, "VBAR_EL3", make_sysreg_key(3, 6, 12, 0, 0), 3, 6, 12, 0, 0, 3, true, true, 64, 0, 0, 0 },
    { SysRegId::SCR_EL3,  "SCR_EL3",  make_sysreg_key(3, 6, 1, 1, 0),  3, 6, 1, 1, 0, 3, true, true,  64, 0x501ULL, 0, 0 },
    { SysRegId::CPTR_EL3, "CPTR_EL3", make_sysreg_key(3, 6, 1, 1, 2),  3, 6, 1, 1, 2, 3, true, true,  64, 0, 0, 0 },
    { SysRegId::MDCR_EL3, "MDCR_EL3", make_sysreg_key(3, 6, 1, 3, 1),  3, 6, 1, 3, 1, 3, true, true,  64, 0, 0, 0 },

    // --- GICv3 System Registers ---
    { SysRegId::ICC_PMR_EL1,     "ICC_PMR_EL1",     make_sysreg_key(3, 0, 4, 6, 0),   3, 0, 4, 6, 0,   1, true,  true,  64, 0, 0, 0 },
    { SysRegId::ICC_IAR0_EL1,    "ICC_IAR0_EL1",    make_sysreg_key(3, 0, 12, 8, 0),  3, 0, 12, 8, 0,  1, true,  false, 64, 0x3FF, 0, 0 },
    { SysRegId::ICC_EOIR0_EL1,   "ICC_EOIR0_EL1",   make_sysreg_key(3, 0, 12, 8, 1),  3, 0, 12, 8, 1,  1, false, true,  64, 0, 0, 0 },
    { SysRegId::ICC_HPPIR0_EL1,  "ICC_HPPIR0_EL1",  make_sysreg_key(3, 0, 12, 8, 2),  3, 0, 12, 8, 2,  1, true,  false, 64, 0x3FF, 0, 0 },
    { SysRegId::ICC_BPR0_EL1,    "ICC_BPR0_EL1",    make_sysreg_key(3, 0, 12, 8, 3),  3, 0, 12, 8, 3,  1, true,  true,  64, 2, 0, 0 },
    { SysRegId::ICC_IAR1_EL1,    "ICC_IAR1_EL1",    make_sysreg_key(3, 0, 12, 12, 0), 3, 0, 12, 12, 0, 1, true,  false, 64, 0x3FF, 0, 0 },
    { SysRegId::ICC_EOIR1_EL1,   "ICC_EOIR1_EL1",   make_sysreg_key(3, 0, 12, 12, 1), 3, 0, 12, 12, 1, 1, false, true,  64, 0, 0, 0 },
    { SysRegId::ICC_HPPIR1_EL1,  "ICC_HPPIR1_EL1",  make_sysreg_key(3, 0, 12, 12, 2), 3, 0, 12, 12, 2, 1, true,  false, 64, 0x3FF, 0, 0 },
    { SysRegId::ICC_BPR1_EL1,    "ICC_BPR1_EL1",    make_sysreg_key(3, 0, 12, 12, 3), 3, 0, 12, 12, 3, 1, true,  true,  64, 3, 0, 0 },
    { SysRegId::ICC_SRE_EL1,     "ICC_SRE_EL1",     make_sysreg_key(3, 0, 12, 12, 5), 3, 0, 12, 12, 5, 1, true,  true,  64, 7, 0, 0 },
    { SysRegId::ICC_IGRPEN0_EL1, "ICC_IGRPEN0_EL1", make_sysreg_key(3, 0, 12, 12, 6), 3, 0, 12, 12, 6, 1, true,  true,  64, 0, 0, 0 },
    { SysRegId::ICC_IGRPEN1_EL1, "ICC_IGRPEN1_EL1", make_sysreg_key(3, 0, 12, 12, 7), 3, 0, 12, 12, 7, 1, true,  true,  64, 0, 0, 0 },
    { SysRegId::ICC_DIR_EL1,     "ICC_DIR_EL1",     make_sysreg_key(3, 0, 12, 11, 1), 3, 0, 12, 11, 1, 1, false, true,  64, 0, 0, 0 },
    { SysRegId::ICC_RPR_EL1,     "ICC_RPR_EL1",     make_sysreg_key(3, 0, 12, 11, 3), 3, 0, 12, 11, 3, 1, true,  false, 64, 0xFF, 0, 0 },
    { SysRegId::ICC_CTLR_EL1,    "ICC_CTLR_EL1",    make_sysreg_key(3, 0, 12, 12, 4), 3, 0, 12, 12, 4, 1, true,  true,  64, 0, 0, 0 },
    { SysRegId::ICC_SRE_EL2,     "ICC_SRE_EL2",     make_sysreg_key(3, 4, 12, 9, 5),  3, 4, 12, 9, 5,  2, true,  true,  64, 7, 0, 0 },
    { SysRegId::ICC_SGI1R_EL1,   "ICC_SGI1R_EL1",   make_sysreg_key(3, 0, 12, 11, 5), 3, 0, 12, 11, 5, 1, false, true,  64, 0, 0, 0 },
    { SysRegId::ICH_HCR_EL2,     "ICH_HCR_EL2",     make_sysreg_key(3, 4, 12, 11, 0), 3, 4, 12, 11, 0, 2, true,  true,  64, 0, 0, 0 },
    { SysRegId::ICH_VTR_EL2,     "ICH_VTR_EL2",     make_sysreg_key(3, 4, 12, 11, 1), 3, 4, 12, 11, 1, 2, true,  false, 64, 0x03ULL, 0, 0 },
    { SysRegId::ICH_MISR_EL2,    "ICH_MISR_EL2",    make_sysreg_key(3, 4, 12, 11, 2), 3, 4, 12, 11, 2, 2, true,  false, 64, 0, 0, 0 },
    { SysRegId::ICH_EISR_EL2,    "ICH_EISR_EL2",    make_sysreg_key(3, 4, 12, 11, 3), 3, 4, 12, 11, 3, 2, true,  false, 64, 0, 0, 0 },
    { SysRegId::ICH_ELRSR_EL2,   "ICH_ELRSR_EL2",   make_sysreg_key(3, 4, 12, 11, 5), 3, 4, 12, 11, 5, 2, true,  false, 64, 0x0F, 0, 0 },
    { SysRegId::ICH_VMCR_EL2,    "ICH_VMCR_EL2",    make_sysreg_key(3, 4, 12, 11, 7), 3, 4, 12, 11, 7, 2, true,  true,  64, 0, 0, 0 },
    { SysRegId::ICH_LR0_EL2,     "ICH_LR0_EL2",     make_sysreg_key(3, 4, 12, 12, 0), 3, 4, 12, 12, 0, 2, true,  true,  64, 0, 0, 0 },
    { SysRegId::ICH_LR1_EL2,     "ICH_LR1_EL2",     make_sysreg_key(3, 4, 12, 12, 1), 3, 4, 12, 12, 1, 2, true,  true,  64, 0, 0, 0 },
    { SysRegId::ICH_LR2_EL2,     "ICH_LR2_EL2",     make_sysreg_key(3, 4, 12, 12, 2), 3, 4, 12, 12, 2, 2, true,  true,  64, 0, 0, 0 },
    { SysRegId::ICH_LR3_EL2,     "ICH_LR3_EL2",     make_sysreg_key(3, 4, 12, 12, 3), 3, 4, 12, 12, 3, 2, true,  true,  64, 0, 0, 0 },

    // --- PMU System Registers ---
    { SysRegId::PMCR_EL0,       "PMCR_EL0",       make_sysreg_key(3, 3, 9, 12, 0), 3, 3, 9, 12, 0, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::PMCNTENSET_EL0, "PMCNTENSET_EL0", make_sysreg_key(3, 3, 9, 12, 1), 3, 3, 9, 12, 1, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::PMCNTENCLR_EL0, "PMCNTENCLR_EL0", make_sysreg_key(3, 3, 9, 12, 2), 3, 3, 9, 12, 2, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::PMOVSCLR_EL0,   "PMOVSCLR_EL0",   make_sysreg_key(3, 3, 9, 12, 3), 3, 3, 9, 12, 3, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::PMSWINC_EL0,    "PMSWINC_EL0",    make_sysreg_key(3, 3, 9, 12, 4), 3, 3, 9, 12, 4, 0, false, true, 64, 0, 0, 0 },
    { SysRegId::PMSELR_EL0,     "PMSELR_EL0",     make_sysreg_key(3, 3, 9, 12, 5), 3, 3, 9, 12, 5, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::PMCEID0_EL0,    "PMCEID0_EL0",    make_sysreg_key(3, 3, 9, 12, 6), 3, 3, 9, 12, 6, 0, true, false, 64, 0, 0, 0 },
    { SysRegId::PMCEID1_EL0,    "PMCEID1_EL0",    make_sysreg_key(3, 3, 9, 12, 7), 3, 3, 9, 12, 7, 0, true, false, 64, 0, 0, 0 },
    { SysRegId::PMCCNTR_EL0,    "PMCCNTR_EL0",    make_sysreg_key(3, 3, 9, 13, 0), 3, 3, 9, 13, 0, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::PMXEVTYPER_EL0, "PMXEVTYPER_EL0", make_sysreg_key(3, 3, 9, 13, 1), 3, 3, 9, 13, 1, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::PMXEVCNTR_EL0,  "PMXEVCNTR_EL0",  make_sysreg_key(3, 3, 9, 13, 2), 3, 3, 9, 13, 2, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::PMUSERENR_EL0,  "PMUSERENR_EL0",  make_sysreg_key(3, 3, 9, 14, 0), 3, 3, 9, 14, 0, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::PMINTENSET_EL1, "PMINTENSET_EL1", make_sysreg_key(3, 0, 9, 14, 1), 3, 0, 9, 14, 1, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::PMINTENCLR_EL1, "PMINTENCLR_EL1", make_sysreg_key(3, 0, 9, 14, 2), 3, 0, 9, 14, 2, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::PMOVSSET_EL0,   "PMOVSSET_EL0",   make_sysreg_key(3, 3, 9, 14, 3), 3, 3, 9, 14, 3, 0, true, true,  64, 0, 0, 0 },
    { SysRegId::PMCCFILTR_EL0,  "PMCCFILTR_EL0",  make_sysreg_key(3, 3, 14, 15, 7), 3, 3, 14, 15, 7, 0, true, true, 64, 0, 0, 0 },

    // --- Debug Architecture Registers ---
    { SysRegId::MDSCR_EL1,        "MDSCR_EL1",        make_sysreg_key(2, 0, 0, 2, 2), 2, 0, 0, 2, 2, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGBVR0_EL1,      "DBGBVR0_EL1",      make_sysreg_key(2, 0, 0, 0, 4), 2, 0, 0, 0, 4, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGBVR1_EL1,      "DBGBVR1_EL1",      make_sysreg_key(2, 0, 0, 1, 4), 2, 0, 0, 1, 4, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGBVR2_EL1,      "DBGBVR2_EL1",      make_sysreg_key(2, 0, 0, 2, 4), 2, 0, 0, 2, 4, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGBVR3_EL1,      "DBGBVR3_EL1",      make_sysreg_key(2, 0, 0, 3, 4), 2, 0, 0, 3, 4, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGBCR0_EL1,      "DBGBCR0_EL1",      make_sysreg_key(2, 0, 0, 0, 5), 2, 0, 0, 0, 5, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGBCR1_EL1,      "DBGBCR1_EL1",      make_sysreg_key(2, 0, 0, 1, 5), 2, 0, 0, 1, 5, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGBCR2_EL1,      "DBGBCR2_EL1",      make_sysreg_key(2, 0, 0, 2, 5), 2, 0, 0, 2, 5, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGBCR3_EL1,      "DBGBCR3_EL1",      make_sysreg_key(2, 0, 0, 3, 5), 2, 0, 0, 3, 5, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGWVR0_EL1,      "DBGWVR0_EL1",      make_sysreg_key(2, 0, 0, 0, 6), 2, 0, 0, 0, 6, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGWVR1_EL1,      "DBGWVR1_EL1",      make_sysreg_key(2, 0, 0, 1, 6), 2, 0, 0, 1, 6, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGWVR2_EL1,      "DBGWVR2_EL1",      make_sysreg_key(2, 0, 0, 2, 6), 2, 0, 0, 2, 6, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGWVR3_EL1,      "DBGWVR3_EL1",      make_sysreg_key(2, 0, 0, 3, 6), 2, 0, 0, 3, 6, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGWCR0_EL1,      "DBGWCR0_EL1",      make_sysreg_key(2, 0, 0, 0, 7), 2, 0, 0, 0, 7, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGWCR1_EL1,      "DBGWCR1_EL1",      make_sysreg_key(2, 0, 0, 1, 7), 2, 0, 0, 1, 7, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGWCR2_EL1,      "DBGWCR2_EL1",      make_sysreg_key(2, 0, 0, 2, 7), 2, 0, 0, 2, 7, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::DBGWCR3_EL1,      "DBGWCR3_EL1",      make_sysreg_key(2, 0, 0, 3, 7), 2, 0, 0, 3, 7, 1, true, true,  64, 0, 0, 0 },
    { SysRegId::ID_AA64DFR0_EL1,  "ID_AA64DFR0_EL1",  make_sysreg_key(3, 0, 0, 5, 0), 3, 0, 0, 5, 0, 1, true, false, 64, 0x0000000000303006ULL, 0, 0 }
};

} // namespace

const SysRegDescriptor* find_descriptor_by_key(std::uint16_t key) noexcept {
    for (const auto& desc : kDescriptors) {
        if (desc.key == key) {
            return &desc;
        }
    }
    return nullptr;
}

const SysRegDescriptor* find_descriptor_by_name(std::string_view name) noexcept {
    for (const auto& desc : kDescriptors) {
        if (name == desc.name) {
            return &desc;
        }
    }
    return nullptr;
}

const SysRegDescriptor* find_descriptor_by_id(SysRegId id) noexcept {
    for (const auto& desc : kDescriptors) {
        if (desc.id == id) {
            return &desc;
        }
    }
    return nullptr;
}

std::span<const SysRegDescriptor> get_all_descriptors() noexcept {
    return kDescriptors;
}

PrivilegeResult check_privilege(const SysRegDescriptor& desc,
                               unsigned current_el,
                               bool is_read,
                               const CPU& cpu) noexcept {
    // 1. Minimum EL enforcement
    if (current_el < desc.min_el) {
        return PrivilegeResult::UndefinedException;
    }

    // 2. Read / Write capability check
    if (is_read && !desc.is_readable) {
        return PrivilegeResult::UndefinedException;
    }
    if (!is_read && !desc.is_writable) {
        return PrivilegeResult::UndefinedException;
    }

    // 3. Special EL0 privilege rules for registers marked accessible at EL0
    if (current_el == 0) {
        // TPIDRRO_EL0 is read-only at EL0
        if (desc.id == SysRegId::TPIDRRO_EL0 && !is_read) {
            return PrivilegeResult::UndefinedException;
        }

        // Generic Timer EL0 access controls via CNTKCTL_EL1
        if (desc.id == SysRegId::CNTFRQ_EL0) {
            if ((cpu.get_cntkctl_el1() & 0x3u) == 0u) {
                return PrivilegeResult::UndefinedException;
            }
        } else if (desc.id == SysRegId::CNTPCT_EL0) {
            if ((cpu.get_cntkctl_el1() & (1u << 0)) == 0u) {
                return PrivilegeResult::UndefinedException;
            }
        } else if (desc.id == SysRegId::CNTVCT_EL0) {
            if ((cpu.get_cntkctl_el1() & (1u << 1)) == 0u) {
                return PrivilegeResult::UndefinedException;
            }
        } else if (desc.id == SysRegId::CNTP_CTL_EL0 || desc.id == SysRegId::CNTP_CVAL_EL0 || desc.id == SysRegId::CNTP_TVAL_EL0) {
            if ((cpu.get_cntkctl_el1() & (1u << 8)) == 0u) {
                return PrivilegeResult::UndefinedException;
            }
        } else if (desc.id == SysRegId::CNTV_CTL_EL0 || desc.id == SysRegId::CNTV_CVAL_EL0 || desc.id == SysRegId::CNTV_TVAL_EL0) {
            if ((cpu.get_cntkctl_el1() & (1u << 9)) == 0u) {
                return PrivilegeResult::UndefinedException;
            }
        }

        // PMU EL0 access rules via PMUSERENR_EL0
        const std::uint32_t userenr = cpu.get_pmu().read_pmuserenr();
        const bool el0_en = (userenr & 0x1u) != 0u; // EN bit
        if (desc.id == SysRegId::PMCCNTR_EL0) {
            const bool cr = (userenr & (1u << 2u)) != 0u;
            if (is_read && !el0_en && !cr) return PrivilegeResult::UndefinedException;
            if (!is_read && !el0_en) return PrivilegeResult::UndefinedException;
        } else if (desc.id == SysRegId::PMXEVCNTR_EL0) {
            const bool er = (userenr & (1u << 3u)) != 0u;
            if (is_read && !el0_en && !er) return PrivilegeResult::UndefinedException;
            if (!is_read && !el0_en) return PrivilegeResult::UndefinedException;
        } else if (desc.id == SysRegId::PMSWINC_EL0) {
            const bool sw = (userenr & (1u << 1u)) != 0u;
            if (is_read) return PrivilegeResult::UndefinedException;
            if (!el0_en && !sw) return PrivilegeResult::UndefinedException;
        } else if (desc.id == SysRegId::PMUSERENR_EL0) {
            if (!is_read || !el0_en) return PrivilegeResult::UndefinedException;
        }
    }

    // 4. EL3 Traps (MDCR_EL3)
    if (current_el < 3) {
        const std::uint64_t mdcr_el3 = cpu.get_mdcr_el3();
        // MDCR_EL3.TPM (bit 6): Trap PMU to EL3
        if ((mdcr_el3 & (1ULL << 6u)) != 0u) {
            if (desc.op0 == 3 && desc.op1 == 3 && desc.crn == 9) {
                return PrivilegeResult::TrapToEl3;
            }
        }
        // MDCR_EL3.TDA (bit 9): Trap Debug Architecture to EL3
        if ((mdcr_el3 & (1ULL << 9u)) != 0u) {
            if (desc.op0 == 2 && desc.op1 == 0 && desc.crn == 0) {
                return PrivilegeResult::TrapToEl3;
            }
        }
    }

    // 5. EL2 Traps (MDCR_EL2)
    if (current_el < 2) {
        const std::uint64_t mdcr_el2 = cpu.get_mdcr_el2();
        // MDCR_EL2.TPM (bit 6): Trap PMU to EL2
        if ((mdcr_el2 & (1ULL << 6u)) != 0u) {
            if (desc.op0 == 3 && desc.op1 == 3 && desc.crn == 9) {
                return PrivilegeResult::TrapToEl2;
            }
        }
        // MDCR_EL2.TPMCR (bit 5): Trap PMCR_EL0 to EL2
        if ((mdcr_el2 & (1ULL << 5u)) != 0u && desc.id == SysRegId::PMCR_EL0) {
            return PrivilegeResult::TrapToEl2;
        }
        // MDCR_EL2.TDA (bit 9): Trap Debug Architecture to EL2
        if ((mdcr_el2 & (1ULL << 9u)) != 0u) {
            if (desc.op0 == 2 && desc.op1 == 0 && desc.crn == 0) {
                return PrivilegeResult::TrapToEl2;
            }
        }
    }

    return PrivilegeResult::Allowed;
}

} // namespace winios::sysreg
