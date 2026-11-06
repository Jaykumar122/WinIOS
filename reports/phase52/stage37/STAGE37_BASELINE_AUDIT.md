# Stage 37 Baseline Audit: MMU Walker, CPACR_EL1 Access Control & Darwin System Register Compatibility

**Project:** WinIOS  
**Phase:** 52  
**Stage:** 37  
**Auditor:** Conformance & Execution Architecture Agent  
**Date:** 2026-11-06  
**Status:** COMPLETE  

---

## 1. Executive Summary

This audit assesses the state of the MMU page table walker, descriptor validation, TLB handling, floating-point/Advanced SIMD access control, snapshot serialization, and system register infrastructure following Stage 36. 

Key findings:
1. **MMU Translation Architecture:** A functional 4-level (L0–L3) stage-1 and stage-2 MMU exists in `mmu.cpp`, supporting 4KB granules, 1GB blocks, 2MB blocks, and 4KB pages. However, descriptor validation does not strictly check for reserved encoding `0b10`, output address alignment violations on block/page base addresses, or translation table base alignment (TTBR0/1).
2. **CPACR_EL1 Defect:** Stage 36 reports noted `CPACR_EL1` in system registers, but code inspection reveals `CPACR_EL1` was completely omitted from `sysreg.h` and `sysreg.cpp`. Furthermore, `CPU` in `cpu.h` lacked member `cpacr_el1_` and accessors. In `smp_core_state.cpp`, `cpacr_el1` was defined in the struct and snapshot buffer, but `capture_from_cpu` and `apply_to_cpu` did not transfer it to or from `CPU`.
3. **Unchecked FP/SIMD Execution:** The instruction decoder in `cpu.cpp` executed scalar FP (FADD, FSUB, FMUL, FDIV, FMOV, FCMP, FCVT, etc.) and SIMD load/store instructions without checking `CPACR_EL1.FPEN` access-control bits.
4. **Missing FP Trap Exception:** `synchronous_exception.h` lacked `ExceptionClass::FpSimdAccessTrap = 0x07`.
5. **System Register Descriptor Registry:** While Stage 36 reported 121 descriptors, `sysreg.cpp` actually contains 89 active descriptors. 
6. **Snapshot Compatibility:** Core architectural state is 1080 bytes. Deserialization relies on a length check (`size >= 1080`) rather than structured section header validation.

---

## 2. Descriptor Classification (Actual Baseline)

Every system register in the baseline registry is classified into one of four architectural tiers:

### 2.1 Fully Implemented (65 registers)
Registers with dedicated storage, full read/write privilege checks, exception routing, and functional execution effects:
- **PSTATE & Control:** `CurrentEL`, `DAIF`, `NZCV`, `SPSel`, `FPCR`, `FPSR`.
- **EL1 Exception Architecture:** `SPSR_EL1`, `ELR_EL1`, `ESR_EL1`, `FAR_EL1`, `VBAR_EL1`, `SP_EL0`.
- **Thread Pointer Registers:** `TPIDR_EL0`, `TPIDR_EL1`, `TPIDRRO_EL0`.
- **Memory Management:** `SCTLR_EL1`, `TCR_EL1`, `TTBR0_EL1`, `TTBR1_EL1`, `MAIR_EL1`, `PAR_EL1`.
- **Generic Timers (EL0/EL1):** `CNTFRQ_EL0`, `CNTPCT_EL0`, `CNTVCT_EL0`, `CNTP_CTL_EL0`, `CNTP_CVAL_EL0`, `CNTP_TVAL_EL0`, `CNTV_CTL_EL0`, `CNTV_CVAL_EL0`, `CNTV_TVAL_EL0`.
- **EL2 Virtualization Core:** `SP_EL1`, `SP_EL2`, `SPSR_EL2`, `ELR_EL2`, `ESR_EL2`, `FAR_EL2`, `HPFAR_EL2`, `VBAR_EL2`, `HCR_EL2`, `VTTBR_EL2`, `VTCR_EL2`, `CNTVOFF_EL2`.
- **EL3 Security Monitor:** `SP_EL3`, `SPSR_EL3`, `ELR_EL3`, `ESR_EL3`, `FAR_EL3`, `VBAR_EL3`, `SCR_EL3`.
- **GICv3 CPU Interface (EL1/EL2):** `ICC_PMR_EL1`, `ICC_IAR0_EL1`, `ICC_EOIR0_EL1`, `ICC_HPPIR0_EL1`, `ICC_BPR0_EL1`, `ICC_IAR1_EL1`, `ICC_EOIR1_EL1`, `ICC_HPPIR1_EL1`, `ICC_BPR1_EL1`, `ICC_SRE_EL1`, `ICC_IGRPEN0_EL1`, `ICC_IGRPEN1_EL1`, `ICC_DIR_EL1`, `ICC_RPR_EL1`, `ICC_CTLR_EL1`, `ICC_SRE_EL2`, `ICC_SGI1R_EL1`.
- **GICv3 Virtual Interface (EL2):** `ICH_HCR_EL2`, `ICH_VTR_EL2`, `ICH_MISR_EL2`, `ICH_EISR_EL2`, `ICH_ELRSR_EL2`, `ICH_VMCR_EL2`, `ICH_LR0_EL2`..`ICH_LR3_EL2`.

### 2.2 Partially Implemented (16 registers)
Registers modeled with storage or constant architectural responses, but without full hardware sub-features:
- **Identification & Features:** `ID_AA64MMFR0_EL1`, `ID_AA64DFR0_EL1`, `DCZID_EL0`, `MPIDR_EL1`.
- **PMU Counters & Controls:** `PMCR_EL0`, `PMCNTENSET_EL0`, `PMCNTENCLR_EL0`, `PMOVSCLR_EL0`, `PMSWINC_EL0`, `PMSELR_EL0`, `PMCCNTR_EL0`, `PMXEVTYPER_EL0`, `PMXEVCNTR_EL0`, `PMUSERENR_EL0`, `PMINTENSET_EL1`, `PMINTENCLR_EL1`, `PMOVSSET_EL0`, `PMCCFILTR_EL0`.
- **Timer Virtualization Controls:** `CNTKCTL_EL1`, `CNTHCTL_EL2`, `CNTHP_CTL_EL2`, `CNTHP_CVAL_EL2`, `CNTHP_TVAL_EL2`.

### 2.3 Metadata-Only (8 registers)
Registered in `kDescriptors` but lacking operational hardware logic beyond register reads/writes:
- `MDSCR_EL1`, `DBGBVR0_EL1`..`3`, `DBGBCR0_EL1`..`3`, `DBGWVR0_EL1`..`3`, `DBGWCR0_EL1`..`3` (partially handled in debug architecture component).
- `CPTR_EL3`, `MDCR_EL2`, `MDCR_EL3`.

### 2.4 Explicitly Unsupported / Missing
- **`CPACR_EL1`:** Missing from registry, CPU core, and instruction trap pipeline. (Remediated in Stage 37).
- **Fine-Grained Traps:** `HSTR_EL2`, `CPTR_EL2`.
- **Vector Extensions:** `ZCR_EL1`, `SVCR` (SVE/SME).
- **Darwin Apple Silicon Registers:** `APSTS_EL1`, `ACC_OVRD_EL1`, `SPRR_CONFIG_EL1`, etc. (Addressed via isolated research registry in Stage 37).

---

## 3. Detailed MMU Baseline Findings

1. **Granule Support:** Strictly 4KB granule ($2^{12}$). TG0=00, TG1=10 supported.
2. **Translation Levels:** L0 (512GB per entry), L1 (1GB blocks or table), L2 (2MB blocks or table), L3 (4KB pages).
3. **Descriptor Validation Gaps:**
   - In `MMU::validate_descriptor()`: The reserved encoding `type_bits == 0x2ULL` (0b10) was not explicitly rejected before switch logic.
   - Alignment validation on block/page base addresses was not performed during table walks (e.g. 1GB block must be $2^{30}$ aligned; 2MB block must be $2^{21}$ aligned; 4KB page must be $2^{12}$ aligned).
   - TTBR base alignment was assumed without validating against translation table size.
4. **Permissions:**
   - AP[2:1] correctly modeled (AP[2]=1 read-only, AP[1]=1 EL0 accessible).
   - UXN (bit 54) and PXN (bit 53) enforced.
   - WXN in SCTLR_EL1 (bit 19) forces UXN=1 and PXN=1 for writable pages.
   - Access Flag AF (bit 10): correctly faults with `MmuFaultType::AccessFlag` if 0.
5. **TLB Behavior:**
   - 64-entry software TLB caching L1 blocks, L2 blocks, and L3 pages.
   - ASID isolation and VMID tagging present.
   - Invalidation operations (`invalidate_tlb_all`, `invalidate_tlb_va`, `invalidate_tlb_asid`, `invalidate_tlb_vaa`) implemented.

---

## 4. Remediation Plan for Stage 37

1. **Objective A:** Harden `validate_descriptor()` to explicitly reject 0b10 reserved encodings, enforce block/page alignment, check 48-bit PA overflow against physical bounds, and generate exact architectural FSC codes for translation/address-size/permission faults.
2. **Objective B & C:**
   - Add `CPACR_EL1` to `sysreg.h`, `sysreg.cpp`, `CPU`, and `smp_core_state`.
   - Add `ExceptionClass::FpSimdAccessTrap = 0x07` and `make_fp_simd_access_trap()`.
   - Insert `is_fp_simd_enabled(current_el)` check in `CPU::step()` before every FP and SIMD instruction.
   - Validate that trapped instructions do not alter registers, flags, or memory, and route to `VBAR_EL1 + 0x400` (from EL0) or `+ 0x000/0x200` (from EL1).
3. **Objective D:** Create isolated `darwin_sysreg_compat.h` / `darwin_sysreg_compat.cpp` defining Darwin/XNU compatibility registry with evidence-based classifications.
4. **Objective E:** Extend `independent_reference_model` with `ReferenceMmuModel` and CPACR_EL1 FP trap checks.
5. **Objective F:** Validate snapshot round-trip, field validation, and deterministic canonical hashing.
6. **Objective G & H:** Execute bounded fuzz testing (seeds `"MMU1"`, `"FPEN"`, `"SNAP"`, 1,000 iterations each) and verify 100% CTest pass in Debug and Release.
