# WinIOS Phase 52 Stage 9: Implemented System Register Inventory

This document provides a comprehensive inventory of all AArch64 system registers implemented in WinIOS, their architectural encodings, access permissions, read/write capabilities, traps, and backing implementations.

---

## 1. Architectural Encoding and Packing Summary

System registers in WinIOS are identified by a 16-bit packed key:
`key = (o0 << 14u) | (op1 << 11u) | (crn << 7u) | (crm << 3u) | op2`
where `o0 = 1` for standard `op0=3` system registers and `o0 = 0` for `op0=2` debug architecture registers.

---

## 2. Implemented Register Inventory

| Register | Key | op0, op1, CRn, CRm, op2 | Supported ELs | Access | Can Trap? | Backing Architectural State |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **PSTATE & Core Registers** | | | | | | |
| `NZCV` | `0x5A10` | 3, 3, 4, 2, 0 | EL0, EL1, EL2, EL3 | RW | No | `nzcv_` (bits [31:28]) |
| `DAIF` | `0x5A11` | 3, 3, 4, 2, 1 | EL1, EL2, EL3 | RW | EL0 undef | `daif_` (bits [9:6]) |
| `CurrentEL` | `0x4212` | 3, 0, 4, 2, 2 | EL1, EL2, EL3 | RO | EL0 undef, write undef | `(current_el_ & 0x3) << 2` |
| `SPSel` | `0x4210` | 3, 0, 4, 2, 0 | EL1, EL2, EL3 | RW | EL0 undef | `sp_sel_` |
| **Exception Handling (EL1)** | | | | | | |
| `SPSR_EL1` | `0x4200` | 3, 0, 4, 0, 0 | EL1, EL2, EL3 | RW | EL0 undef | `spsr_el1_` |
| `ELR_EL1` | `0x4201` | 3, 0, 4, 0, 1 | EL1, EL2, EL3 | RW | EL0 undef | `elr_el1_` |
| `SP_EL0` | `0x4208` | 3, 0, 4, 1, 0 | EL1, EL2, EL3 | RW | EL0 undef | `sp_el0_` |
| `ESR_EL1` | `0x4290` | 3, 0, 5, 2, 0 | EL1, EL2, EL3 | RW | EL0 undef | `esr_el1_` |
| `FAR_EL1` | `0x4300` | 3, 0, 6, 0, 0 | EL1, EL2, EL3 | RW | EL0 undef | `far_el1_` |
| `VBAR_EL1` | `0x4600` | 3, 0, 12, 0, 0 | EL1, EL2, EL3 | RW | EL0 undef | `vbar_el1_` |
| **FP / SIMD Registers** | | | | | | |
| `FPCR` | `0x5A20` | 3, 3, 4, 4, 0 | EL0, EL1, EL2, EL3 | RW | No | `fpcr_` |
| `FPSR` | `0x5A21` | 3, 3, 4, 4, 1 | EL0, EL1, EL2, EL3 | RW | No | `fpsr_` |
| **MMU & System Control (EL1)**| | | | | | |
| `SCTLR_EL1` | `0x4080` | 3, 0, 1, 0, 0 | EL1, EL2, EL3 | RW | EL0 undef | `mmu_.sctlr_el1` |
| `TCR_EL1` | `0x4102` | 3, 0, 2, 0, 2 | EL1, EL2, EL3 | RW | EL0 undef | `mmu_.tcr_el1` |
| `TTBR0_EL1` | `0x4100` | 3, 0, 2, 0, 0 | EL1, EL2, EL3 | RW | EL0 undef | `mmu_.ttbr0_el1` |
| `TTBR1_EL1` | `0x4101` | 3, 0, 2, 0, 1 | EL1, EL2, EL3 | RW | EL0 undef | `mmu_.ttbr1_el1` |
| `MAIR_EL1` | `0x4510` | 3, 0, 10, 2, 0 | EL1, EL2, EL3 | RW | EL0 undef | `mmu_.mair_el1` |
| `PAR_EL1` | `0x43A0` | 3, 0, 7, 4, 0 | EL1, EL2, EL3 | RW | EL0 undef | `mmu_.par_el1` |
| `ID_AA64MMFR0_EL1`| `0x4038` | 3, 0, 0, 7, 0 | EL1, EL2, EL3 | RO | EL0 undef, write undef | `mmu_.id_aa64mmfr0_el1` |
| `DCZID_EL0` | `0x5807` | 3, 3, 0, 0, 7 | EL0, EL1, EL2, EL3 | RO | Write undef | Constant `0x4` (Block size 64B) |
| **Hypervisor & Virtualization (EL2)** | | | | | | |
| `SP_EL1` | `0x6208` | 3, 4, 4, 1, 0 | EL2, EL3 | RW | <EL2 undef | `sp_el1_` |
| `SP_EL2` | `0x7208` | 3, 6, 4, 1, 0 | EL3 | RW | <EL3 undef | `sp_el2_` |
| `SPSR_EL2` | `0x6200` | 3, 4, 4, 0, 0 | EL2, EL3 | RW | <EL2 undef | `spsr_el2_` |
| `ELR_EL2` | `0x6201` | 3, 4, 4, 0, 1 | EL2, EL3 | RW | <EL2 undef | `elr_el2_` |
| `ESR_EL2` | `0x6290` | 3, 4, 5, 2, 0 | EL2, EL3 | RW | <EL2 undef | `esr_el2_` |
| `FAR_EL2` | `0x6300` | 3, 4, 6, 0, 0 | EL2, EL3 | RW | <EL2 undef | `far_el2_` |
| `HPFAR_EL2` | `0x6304` | 3, 4, 6, 0, 4 | EL2, EL3 | RW | <EL2 undef | `hpfar_el2_` |
| `VBAR_EL2` | `0x6600` | 3, 4, 12, 0, 0 | EL2, EL3 | RW | <EL2 undef | `vbar_el2_` |
| `HCR_EL2` | `0x6088` | 3, 4, 1, 1, 0 | EL2, EL3 | RW | <EL2 undef | `hcr_el2_`, `mmu_.hcr_el2` |
| `VTTBR_EL2` | `0x6108` | 3, 4, 2, 1, 0 | EL2, EL3 | RW | <EL2 undef | `mmu_.vttbr_el2` |
| `VTCR_EL2` | `0x610A` | 3, 4, 2, 1, 2 | EL2, EL3 | RW | <EL2 undef | `mmu_.vtcr_el2` |
| `MDCR_EL2` | `0x6089` | 3, 4, 1, 1, 1 | EL2, EL3 | RW | <EL2 undef | `mdcr_el2_` |
| `CNTVOFF_EL2` | `0x6703` | 3, 4, 14, 0, 3 | EL2, EL3 | RW | <EL2 undef | `virtual_time_->cntvoff_el2` |
| `CNTHCTL_EL2` | `0x6708` | 3, 4, 14, 1, 0 | EL2, EL3 | RW | <EL2 undef | `virtual_time_->cnthctl_el2` |
| `CNTHP_CTL_EL2` | `0x6711` | 3, 4, 14, 2, 1 | EL2, EL3 | RW | <EL2 undef | `cnthp_timer_.enable/imask` |
| `CNTHP_CVAL_EL2`| `0x6712` | 3, 4, 14, 2, 2 | EL2, EL3 | RW | <EL2 undef | `cnthp_timer_.cval` |
| `CNTHP_TVAL_EL2`| `0x6710` | 3, 4, 14, 2, 0 | EL2, EL3 | RW | <EL2 undef | `virtual_time_->cnthp_tval` |
| **Generic Timers (EL0 & EL1)** | | | | | | |
| `CNTFRQ_EL0` | `0x5700` | 3, 3, 14, 0, 0 | EL0*, EL1, EL2, EL3 | RW | `CNTKCTL` | `virtual_time_->frequency` |
| `CNTPCT_EL0` | `0x5701` | 3, 3, 14, 0, 1 | EL0*, EL1, EL2, EL3 | RO | `CNTKCTL`, `CNTHCTL` | Physical counter value |
| `CNTVCT_EL0` | `0x5702` | 3, 3, 14, 0, 2 | EL0*, EL1, EL2, EL3 | RO | `CNTKCTL`, `CNTHCTL` | Virtual counter value |
| `CNTKCTL_EL1` | `0x4708` | 3, 0, 14, 1, 0 | EL1, EL2, EL3 | RW | EL0 undef | `virtual_time_->cntkctl_el1` |
| `CNTP_CTL_EL0` | `0x5711` | 3, 3, 14, 2, 1 | EL0*, EL1, EL2, EL3 | RW | `CNTKCTL`, `CNTHCTL` | `phys_timer_.enable/imask` |
| `CNTP_CVAL_EL0`| `0x5712` | 3, 3, 14, 2, 2 | EL0*, EL1, EL2, EL3 | RW | `CNTKCTL`, `CNTHCTL` | `phys_timer_.cval` |
| `CNTP_TVAL_EL0`| `0x5710` | 3, 3, 14, 2, 0 | EL0*, EL1, EL2, EL3 | RW | `CNTKCTL`, `CNTHCTL` | `virtual_time_->phys_tval` |
| `CNTV_CTL_EL0` | `0x5719` | 3, 3, 14, 3, 1 | EL0*, EL1, EL2, EL3 | RW | `CNTKCTL`, `CNTHCTL` | `virt_timer_.enable/imask` |
| `CNTV_CVAL_EL0`| `0x571A` | 3, 3, 14, 3, 2 | EL0*, EL1, EL2, EL3 | RW | `CNTKCTL`, `CNTHCTL` | `virt_timer_.cval` |
| `CNTV_TVAL_EL0`| `0x5718` | 3, 3, 14, 3, 0 | EL0*, EL1, EL2, EL3 | RW | `CNTKCTL`, `CNTHCTL` | `virtual_time_->virt_tval` |
| **Multiprocessor & GICv3 (EL1/EL2)** | | | | | | |
| `MPIDR_EL1` | `0x4005` | 3, 0, 0, 0, 5 | EL1, EL2, EL3 | RO | EL0 undef, write undef | `get_mpidr_el1()` |
| `ICC_SGI1R_EL1` | `0x465D` | 3, 0, 12, 11, 5| EL1, EL2, EL3 | WO | EL0 undef, read undef | `gic_.send_sgi()` |
| `ICC_PMR_EL1` | `0x4230` | 3, 0, 4, 6, 0 | EL1, EL2, EL3 | RW | EL0 undef | `gic_.icc_[core].pmr` |
| `ICC_IAR0_EL1` | `0x4640` | 3, 0, 12, 8, 0 | EL1, EL2, EL3 | RO | EL0 undef, write undef | `gic_.acknowledge_group0()` |
| `ICC_EOIR0_EL1` | `0x4641` | 3, 0, 12, 8, 1 | EL1, EL2, EL3 | WO | EL0 undef, read undef | `gic_.end_of_interrupt_group0()`|
| `ICC_HPPIR0_EL1`| `0x4642` | 3, 0, 12, 8, 2 | EL1, EL2, EL3 | RO | EL0 undef, write undef | `gic_.read_hppir0()` |
| `ICC_BPR0_EL1` | `0x4643` | 3, 0, 12, 8, 3 | EL1, EL2, EL3 | RW | EL0 undef | `gic_.read/write_icc_bpr0()` |
| `ICC_IAR1_EL1` | `0x4660` | 3, 0, 12, 12, 0| EL1, EL2, EL3 | RO | EL0 undef, write undef | `gic_.acknowledge_group1()` |
| `ICC_EOIR1_EL1` | `0x4661` | 3, 0, 12, 12, 1| EL1, EL2, EL3 | WO | EL0 undef, read undef | `gic_.end_of_interrupt_group1()`|
| `ICC_HPPIR1_EL1`| `0x4662` | 3, 0, 12, 12, 2| EL1, EL2, EL3 | RO | EL0 undef, write undef | `gic_.read_hppir1()` |
| `ICC_BPR1_EL1` | `0x4663` | 3, 0, 12, 12, 3| EL1, EL2, EL3 | RW | EL0 undef | `gic_.read/write_icc_bpr1()` |
| `ICC_SRE_EL1` | `0x4665` | 3, 0, 12, 12, 5| EL1, EL2, EL3 | RW | EL0 undef | `gic_.read/write_icc_sre_el1()` |
| `ICC_IGRPEN0_EL1`| `0x4666`| 3, 0, 12, 12, 6| EL1, EL2, EL3 | RW | EL0 undef | `gic_.read/write_icc_igrpen0()` |
| `ICC_IGRPEN1_EL1`| `0x4667`| 3, 0, 12, 12, 7| EL1, EL2, EL3 | RW | EL0 undef | `gic_.read/write_icc_igrpen1()` |
| `ICC_DIR_EL1` | `0x4659` | 3, 0, 12, 11, 1| EL1, EL2, EL3 | WO | EL0 undef, read undef | `gic_.write_icc_dir()` |
| `ICC_RPR_EL1` | `0x465B` | 3, 0, 12, 11, 3| EL1, EL2, EL3 | RO | EL0 undef, write undef | `gic_.read_icc_rpr()` |
| `ICC_CTLR_EL1` | `0x4664` | 3, 0, 12, 12, 4| EL1, EL2, EL3 | RW | EL0 undef | `gic_.read/write_icc_ctlr()` |
| `ICC_SRE_EL2` | `0x664D` | 3, 4, 12, 9, 5 | EL2, EL3 | RW | <EL2 undef | `gic_.read/write_icc_sre_el2()` |
| `ICH_HCR_EL2` | `0x6658` | 3, 4, 12, 11, 0| EL2, EL3 | RW | <EL2 undef | `gic_.read/write_ich_hcr()` |
| `ICH_VTR_EL2` | `0x6659` | 3, 4, 12, 11, 1| EL2, EL3 | RO | <EL2 undef, write undef | `gic_.read_ich_vtr()` |
| `ICH_MISR_EL2` | `0x665A` | 3, 4, 12, 11, 2| EL2, EL3 | RO | <EL2 undef, write undef | `gic_.read_ich_misr()` |
| `ICH_EISR_EL2` | `0x665B` | 3, 4, 12, 11, 3| EL2, EL3 | RO | <EL2 undef, write undef | `gic_.read_ich_eisr()` |
| `ICH_ELRSR_EL2` | `0x665D` | 3, 4, 12, 11, 5| EL2, EL3 | RO | <EL2 undef, write undef | `gic_.read_ich_elrsr()` |
| `ICH_VMCR_EL2` | `0x665F` | 3, 4, 12, 11, 7| EL2, EL3 | RW | <EL2 undef | `gic_.read/write_ich_vmcr()` |
| `ICH_LR0_EL2` | `0x6660` | 3, 4, 12, 12, 0| EL2, EL3 | RW | <EL2 undef | `gic_.read/write_ich_lr(0)` |
| `ICH_LR1_EL2` | `0x6661` | 3, 4, 12, 12, 1| EL2, EL3 | RW | <EL2 undef | `gic_.read/write_ich_lr(1)` |
| `ICH_LR2_EL2` | `0x6662` | 3, 4, 12, 12, 2| EL2, EL3 | RW | <EL2 undef | `gic_.read/write_ich_lr(2)` |
| `ICH_LR3_EL2` | `0x6663` | 3, 4, 12, 12, 3| EL2, EL3 | RW | <EL2 undef | `gic_.read/write_ich_lr(3)` |
| **Secure Firmware (EL3)** | | | | | | |
| `SCR_EL3` | `0x7088` | 3, 6, 1, 1, 0 | EL3 only | RW | <EL3 undef | `scr_el3_` |
| `CPTR_EL3` | `0x708A` | 3, 6, 1, 1, 2 | EL3 only | RW | <EL3 undef | `cptr_el3_` |
| `MDCR_EL3` | `0x7099` | 3, 6, 1, 3, 1 | EL3 only | RW | <EL3 undef | `mdcr_el3_` |
| `SPSR_EL3` | `0x7200` | 3, 6, 4, 0, 0 | EL3 only | RW | <EL3 undef | `spsr_el3_` |
| `ELR_EL3` | `0x7201` | 3, 6, 4, 0, 1 | EL3 only | RW | <EL3 undef | `elr_el3_` |
| `ESR_EL3` | `0x7290` | 3, 6, 5, 2, 0 | EL3 only | RW | <EL3 undef | `esr_el3_` |
| `FAR_EL3` | `0x7300` | 3, 6, 6, 0, 0 | EL3 only | RW | <EL3 undef | `far_el3_` |
| `VBAR_EL3` | `0x7600` | 3, 6, 12, 0, 0 | EL3 only | RW | <EL3 undef | `vbar_el3_` |
| **Performance Monitoring Unit (PMU)** | | | | | | |
| `PMCR_EL0` | `0x5CE0` | 3, 3, 9, 12, 0 | EL0*, EL1, EL2, EL3 | RW | `PMUSERENR`, `MDCR` | `pmu_.read/write_pmcr()` |
| `PMCNTENSET_EL0`| `0x5CE1` | 3, 3, 9, 12, 1 | EL0*, EL1, EL2, EL3 | RW | `PMUSERENR`, `MDCR` | `pmu_.read/write_pmcntenset()` |
| `PMCNTENCLR_EL0`| `0x5CE2` | 3, 3, 9, 12, 2 | EL0*, EL1, EL2, EL3 | RW | `PMUSERENR`, `MDCR` | `pmu_.read/write_pmcntenclr()` |
| `PMOVSCLR_EL0` | `0x5CE3` | 3, 3, 9, 12, 3 | EL0*, EL1, EL2, EL3 | RW | `PMUSERENR`, `MDCR` | `pmu_.read/write_pmovsclr()` |
| `PMSWINC_EL0` | `0x5CE4` | 3, 3, 9, 12, 4 | EL0*, EL1, EL2, EL3 | RW | `PMUSERENR`, `MDCR` | `pmu_.write_pmswinc()`, read=0 |
| `PMSELR_EL0` | `0x5CE5` | 3, 3, 9, 12, 5 | EL0*, EL1, EL2, EL3 | RW | `PMUSERENR`, `MDCR` | `pmu_.read/write_pmselr()` |
| `PMCEID0_EL0` | `0x5CE6` | 3, 3, 9, 12, 6 | EL0*, EL1, EL2, EL3 | RO | `PMUSERENR`, `MDCR` | `pmu_.read_pmceid0()` |
| `PMCEID1_EL0` | `0x5CE7` | 3, 3, 9, 12, 7 | EL0*, EL1, EL2, EL3 | RO | `PMUSERENR`, `MDCR` | `pmu_.read_pmceid1()` |
| `PMCCNTR_EL0` | `0x5CE8` | 3, 3, 9, 13, 0 | EL0*, EL1, EL2, EL3 | RW | `PMUSERENR`, `MDCR` | `pmu_.read/write_pmccntr()` |
| `PMXEVTYPER_EL0`| `0x5CE9` | 3, 3, 9, 13, 1 | EL0*, EL1, EL2, EL3 | RW | `PMUSERENR`, `MDCR` | `pmu_.read/write_pmxevtyper()` |
| `PMXEVCNTR_EL0` | `0x5CEA` | 3, 3, 9, 13, 2 | EL0*, EL1, EL2, EL3 | RW | `PMUSERENR`, `MDCR` | `pmu_.read/write_pmxevcntr()` |
| `PMUSERENR_EL0` | `0x5CF0` | 3, 3, 9, 14, 0 | EL0(RO)*, EL1..3(RW) | RW | Write at EL0 undef | `pmu_.read/write_pmuserenr()` |
| `PMINTENSET_EL1`| `0x44F1` | 3, 0, 9, 14, 1 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `pmu_.read/write_pmintenset()` |
| `PMINTENCLR_EL1`| `0x44F2` | 3, 0, 9, 14, 2 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `pmu_.read/write_pmintenclr()` |
| `PMOVSSET_EL0` | `0x5CF3` | 3, 3, 9, 14, 3 | EL0*, EL1, EL2, EL3 | RW | `PMUSERENR`, `MDCR` | `pmu_.read/write_pmovsset()` |
| `PMCCFILTR_EL0` | `0x5F7F` | 3, 3, 14, 15, 7| EL0*, EL1, EL2, EL3 | RW | `PMUSERENR`, `MDCR` | `pmu_.read/write_pmccfiltr()` |
| `PMEVCNTR<n>_EL0`| `0x5F40..5F`| 3, 3, 14, 8..11, 0..7 | EL0*, EL1, EL2, EL3 | RW | `PMUSERENR`, `MDCR` | `pmu_.read/write_pmevcntr(n)` |
| `PMEVTYPER<n>_EL0`|`0x5F60..7E`| 3, 3, 14, 12..15, 0..7| EL0*, EL1, EL2, EL3 | RW | `PMUSERENR`, `MDCR` | `pmu_.read/write_pmevtyper(n)` |
| **Debug Architecture (ARMv8)** | | | | | | |
| `MDSCR_EL1` | `0x0012` | 2, 0, 0, 2, 2 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.mdscr_el1` |
| `DBGBVR0_EL1` | `0x0004` | 2, 0, 0, 0, 4 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgbvr[0]` |
| `DBGBVR1_EL1` | `0x000C` | 2, 0, 0, 1, 4 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgbvr[1]` |
| `DBGBVR2_EL1` | `0x0014` | 2, 0, 0, 2, 4 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgbvr[2]` |
| `DBGBVR3_EL1` | `0x001C` | 2, 0, 0, 3, 4 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgbvr[3]` |
| `DBGBCR0_EL1` | `0x0005` | 2, 0, 0, 0, 5 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgbcr[0]` |
| `DBGBCR1_EL1` | `0x000D` | 2, 0, 0, 1, 5 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgbcr[1]` |
| `DBGBCR2_EL1` | `0x0015` | 2, 0, 0, 2, 5 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgbcr[2]` |
| `DBGBCR3_EL1` | `0x001D` | 2, 0, 0, 3, 5 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgbcr[3]` |
| `DBGWVR0_EL1` | `0x0006` | 2, 0, 0, 0, 6 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgwvr[0]` |
| `DBGWVR1_EL1` | `0x000E` | 2, 0, 0, 1, 6 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgwvr[1]` |
| `DBGWVR2_EL1` | `0x0016` | 2, 0, 0, 2, 6 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgwvr[2]` |
| `DBGWVR3_EL1` | `0x001E` | 2, 0, 0, 3, 6 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgwvr[3]` |
| `DBGWCR0_EL1` | `0x0007` | 2, 0, 0, 0, 7 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgwcr[0]` |
| `DBGWCR1_EL1` | `0x000F` | 2, 0, 0, 1, 7 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgwcr[1]` |
| `DBGWCR2_EL1` | `0x0017` | 2, 0, 0, 2, 7 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgwcr[2]` |
| `DBGWCR3_EL1` | `0x001F` | 2, 0, 0, 3, 7 | EL1, EL2, EL3 | RW | EL0 undef, `MDCR` | `debug_arch_.dbgwcr[3]` |
| `ID_AA64DFR0_EL1`| `0x4028` | 3, 0, 0, 5, 0 | EL1, EL2, EL3 | RO | EL0 undef, write undef | BRPs=3, WRPs=3, DebugVer=6 |

*(Note: EL0* indicates access permitted subject to enabling bits in control registers like `CNTKCTL_EL1` or `PMUSERENR_EL0`)*
