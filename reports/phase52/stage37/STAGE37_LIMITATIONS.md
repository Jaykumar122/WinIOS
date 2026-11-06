# WinIOS — Phase 52, Stage 37: Limitations & Conformance Boundary Report

## 1. Executive Summary

Phase 52, Stage 37 established architectural hardening for the AArch64 MMU translation-table walker, implemented `CPACR_EL1` FP/SIMD access control with architectural exception trapping, bound `CPACR_EL1` into SMP snapshot/replay state, and created an isolated research catalog for Darwin-specific system registers.

This document explicitly defines the boundaries, constraints, supported features, and unsupported capabilities of WinIOS at the conclusion of Stage 37.

---

## 2. MMU Translation-Table Walker Conformance

### 2.1 Supported Features
- **Translation Granule:** 4KB granule supported across Level 0, Level 1, Level 2, and Level 3 translation walks.
- **Physical Address Space:** 48-bit maximum physical address range (`0x0000_0000_0000` to `0x0000_FFFF_FFFF_FFFF`). Addresses with bits [63:48] != 0 are rejected.
- **Descriptor Type Validation:**
  - Level 0: Table descriptors only (`0b11`). Block descriptors are strictly rejected.
  - Level 1: Table descriptors (`0b11`) and 1GB Block descriptors (`0b01`).
  - Level 2: Table descriptors (`0b11`) and 2MB Block descriptors (`0b01`).
  - Level 3: 4KB Page descriptors (`0b11`) only. Table descriptors (`0b01` or reserved `0b10`) are strictly rejected.
  - Reserved descriptor type `0b10` is rejected across all levels.
  - Unmapped/invalid descriptors (`bit[0] == 0`) trigger Level-specific Translation Faults.
- **Block Alignment Validation:**
  - Level 1 (1GB Block): Bits [29:12] must be 0 (mask `0x3FFFF000ULL`). Misalignment triggers Address Size / Translation Fault.
  - Level 2 (2MB Block): Bits [20:12] must be 0 (mask `0x001FF000ULL`). Misalignment triggers Address Size / Translation Fault.
- **Permission Checking:**
  - Access permissions AP[2:1] enforced against Current EL (EL0 user vs EL1 privileged) for data read and write accesses.
  - Unprivileged Read/Write, Privileged-only Read/Write, and Read-Only enforcement.
  - Instruction execute permissions: UXN (Unprivileged Execute-Never) and PXN (Privileged Execute-Never) enforced on instruction fetches.
- **Fault Syndromes & Recording:**
  - Accurate Level 0–3 Translation Faults (`0x04`–`0x07`), Level 1–3 Permission Faults (`0x0D`–`0x0F`), and Alignment Faults.
  - `ESR_EL1` populated with standard EC (`0x20`/`0x21` for Instruction/Data Abort), IFSC/DFSC, and WnR flags.
  - `FAR_EL1` updated with the exact faulting virtual address.
- **TLB Operations:**
  - Software TLB caching for virtual-to-physical address mappings.
  - Invalidation support via `TLBI VMALLE1`, `TLBI VMALLE1IS`, `TLBI VAAE1`, and `TLBI VAALE1`.

### 2.2 Unsupported Features / Known Limitations
- **Alternative Granules:** 16KB and 64KB translation granules (`TCR_EL1.TG0` / `TG1`) are currently unsupported. Attempts to configure non-4KB granules are not modeled.
- **LPA / 52-bit Addressing:** Large Physical Address (LPA) 52-bit addressing (ARMv8.2-A) is unsupported; physical addresses are clamped/verified against 48 bits.
- **Stage 2 Translation:** EL2 (Hypervisor) Stage 2 translation is not implemented. All translations assume Stage 1 single-stage translation.
- **Security Domains:** TrustZone / Secure vs Non-Secure physical address spaces are not differentiated.
- **Hierarchical Table Attributes:** Table descriptor bits `APTable`, `UXNTable`, and `PXNTable` hierarchical permissions are not evaluated during table walking.
- **Hardware Access Flag & Dirty Bit Management:** Hardware AF (`TCR_EL1.HA`) and DBM (`TCR_EL1.HD`) are unsupported; software must maintain access/dirty flags.
- **Contiguous Bit:** Bit [52] contiguous block/page hints are ignored.
- **MAIR_EL1 Memory Attributes:** Memory attribute indirection (Device-nGnRnE, Normal Write-Back Cacheable, etc.) is not modeled at the bus level; memory accesses resolve directly against virtual RAM/MMIO buffers.

---

## 3. FP/SIMD Access Control & Architectural Exceptions

### 3.1 Supported Features
- **Register Architecture:** `CPACR_EL1` (CRn=1, CRm=0, op1=0, op2=2, encoding `0x4082`, min_el=1, RW).
- **Reset State:** Architectural reset value is strictly `0x0ULL` (FPEN=0b00).
- **FPEN Decode Semantics (bits [21:20]):**
  - `0b00`: All FP/SIMD instruction executions at EL0 and EL1 trap to EL1.
  - `0b01`: FP/SIMD instruction executions at EL0 trap to EL1. EL1 executions succeed.
  - `0b10`: All FP/SIMD instruction executions at EL0 and EL1 trap to EL1.
  - `0b11`: Full FP/SIMD instruction execution permitted at EL0 and EL1.
- **Exception Trap Generation:**
  - Generates synchronous exception with Exception Class `EC = 0x07` (`ExceptionClass::FpSimdAccessTrap`).
  - Sets `ESR_EL1` to architectural trap syndrome `0x1E000000u`.
  - Vectors to `VBAR_EL1 + 0x400` when trapping from Lower EL (EL0).
  - Vectors to `VBAR_EL1 + 0x200` when trapping from Current EL (EL1 with SP_ELx).
  - Saves pre-instruction PC into `ELR_EL1` and PSTATE into `SPSR_EL1`.
- **Architectural State Preservation:**
  - Trapping instructions abort before state mutation; target register (e.g., `Vd` or `Xd`) values remain unchanged.
- **Instruction Coverage:**
  - Evaluated before decoding FP data processing (scalar float add/sub/mul/div/sqrt/fma/compare/convert), FP load/store (scalar and vector), Advanced SIMD integer arithmetic, SIMD shift/permute/reduction, and cryptographic extensions (AES/SHA).
- **Snapshot Integration:**
  - `cpacr_el1_` persisted in the canonical 1080-byte `SmpCoreState` per-core snapshot payload, preserving exact trap state across `.wsnap v2` checkpoints.

### 3.2 Unsupported Features / Known Limitations
- **SVE / SVE2:** Scalable Vector Extensions are not implemented (`CPACR_EL1.ZEN` bits [17:16] and `ZCR_EL1` are unmodeled).
- **SME:** Scalable Matrix Extensions are not implemented (`CPACR_EL1.SMEN` bits [25:24] are unmodeled).
- **Higher Privilege Traps:** EL2 (`CPTR_EL2.TFP`) and EL3 (`CPTR_EL3.TFP`) FP trapping are not implemented because EL2 and EL3 execution levels are not modeled.
- **FPCR Exception Masking:** Trapping of IEEE-754 floating-point exceptions (Invalid Operation, Division by Zero, Overflow, Underflow, Inexact) via `FPCR` bits [12:8] is not modeled; operations follow host IEEE-754 default non-trapping NaN/infinity propagation.

---

## 4. Darwin System Register Compatibility Layer

### 4.1 Boundary & Architecture
- **Research-Only Isolation:** The `DarwinCompatibilityRegistry` is strictly isolated within `src/cpu/darwin_sysreg_compat.h` and `src/cpu/darwin_sysreg_compat.cpp`.
- **Instruction Pipeline Decoupling:** The Darwin compatibility registry is **not** registered in the primary CPU `SysRegRegistry` (`sysreg.cpp`). The CPU instruction decoder does not dispatch MSR/MRS to Apple-proprietary registers.
- **Catalog Scope:** Exactly 20 registers across 4 categories:
  - `GenericArchitectural` (5 registers): `ACTLR_EL1`, `AFSR0_EL1`, `AFSR1_EL1`, `AMAIR_EL1`, `CONTEXTIDR_EL1`.
  - `PubliclyDocumentedDarwin` (5 registers): `TPIDRRO_EL0`, `APGKey_EL1`, `DARWIN_THREAD_SWITCH_EL0`, `DARWIN_MACH_PORT_EL0`, `DARWIN_COMM_PAGE_EL0`.
  - `ResearchOnly` (6 registers): `APPLE_HID0`, `APPLE_HID1`, `APPLE_HID4`, `APPLE_HID5`, `APPLE_HID11`, `APPLE_CYC_OVRD`.
  - `Unsupported` (4 registers): `APPLE_ACC_OVRD`, `APPLE_SPRR_CONFIG_EL1`, `APPLE_GXF_CONFIG_EL1`, `APPLE_AMX_CONFIG_EL1`.
- **Integrity:** Configuration registry protected by deterministic CRC32C hash (`0x71661cb5`).

### 4.2 Explicit Non-Claims
- **No Darwin Kernel Boot:** WinIOS does **not** support booting or executing Darwin, macOS, or iOS kernel images (XNU).
- **No Mach-O Execution:** WinIOS does not support loading Mach-O executables, dyld shared caches, or launching Darwin userland processes.
- **No Proprietary Silicon Emulation:** Apple-specific proprietary coprocessors, AMX matrix units, GXF guard execution frameworks, or Apple Neural Engine (ANE) are completely unsupported and unmodeled.
- **Purpose:** The catalog serves exclusively as an architectural research classification to document system-register deltas between standard ARMv8-A and Darwin/Apple Silicon platforms.

---

## 5. Verification Status & Conformance Summary

| Component | Status | Test Suites | Test Results |
|:---|:---:|:---:|:---:|
| MMU Walker Hardening | **VERIFIED** | `MmuCpacrConformanceTests.MmuDescriptorValidation`, `MmuDifferentialTranslation` | 100% Pass |
| 1GB/2MB Block Alignment | **VERIFIED** | `MmuCpacrConformanceTests.MmuBlockAlignmentValidation` | 100% Pass |
| Permissions & Fault Syndromes | **VERIFIED** | `MmuCpacrConformanceTests.MmuPermissionAndFaultSyndromes` | 100% Pass |
| TLB Invalidation | **VERIFIED** | `MmuCpacrConformanceTests.MmuTlbInvalidationSemantics` | 100% Pass |
| CPACR_EL1 Access Control | **VERIFIED** | `MmuCpacrConformanceTests.CpacrAccessControl` | 100% Pass |
| FP/SIMD Trap Vectoring & Syndrome | **VERIFIED** | `MmuCpacrConformanceTests.FpSimdTrapExecution` | 100% Pass |
| Architectural Register Preservation | **VERIFIED** | `MmuCpacrConformanceTests.FpStatePreservationOnTrap` | 100% Pass |
| Darwin SysReg Compatibility Registry | **VERIFIED** | `MmuCpacrConformanceTests.DarwinSysRegRegistryIntegrity` | 100% Pass |
| Snapshot Serialization & Restore | **VERIFIED** | `MmuCpacrConformanceTests.SnapshotRestoreCpacrPersistence` | 100% Pass |
| MMU / CPACR / Snapshot Fuzzing | **VERIFIED** | `MmuCpacrConformanceTests.MmuCpacrFuzzStress` (3,000 iterations) | 100% Pass |
| Overall Regression Test Suite | **VERIFIED** | 96 CTest Suites (Debug & Release) | 95 Passed, 1 Skipped |
