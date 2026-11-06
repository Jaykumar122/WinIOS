# Stage 36 Baseline Audit: System Register Architecture, Privilege Enforcement & CPU Execution Conformance

**Project:** WinIOS  
**Phase:** 52  
**Stage:** 36  
**Auditor:** Automated Conformance & Architecture Agent  
**Date:** 2026-11-06  
**Status:** COMPLETE  

---

## 1. Executive Summary

This audit assesses the state of CPU system register decoding, architectural privilege checks, execution semantics, and snapshot/replay state fidelity prior to the Stage 36 implementation. While Stages 26 through 35 established robust SMP scheduling, deterministic DMA, crash-consistent replay, and virtual exception routing, the architectural system-register infrastructure retained ad-hoc dispatch patterns, incomplete thread-pointer register modeling, and fragmented privilege validation.

---

## 2. Exhaustive Baseline Audit Findings

### 2.1 MRS / MSR Instruction Decoder & Implementation
- **Prior State:** MRS and MSR were decoded through a combination of switch blocks in `cpu.cpp` matching packed 16-bit register keys (`((op0 == 2 ? 0 : 1) << 14) | (op1 << 11) | (CRn << 7) | (CRm << 3) | op2`). Several registers were decoded ad-hoc without formal descriptor lookup.
- **Finding:** No centralized registry describing read/write masks, minimum privilege levels, or architectural reset values existed. Unsupported register encodings were handled unevenly across instruction execution paths.
- **Severity:** High (Architectural Correctness).

### 2.2 CurrentEL Representation & Transitions
- **Prior State:** Stored as `current_el_` in `CPU` (values 0, 1, 2, 3), represented architecturally in `CurrentEL` system register bits [3:2].
- **Finding:** Direct field was maintained reliably, but read access via `CurrentEL` register key `0x6442` (`op0=3, op1=0, CRn=4, CRm=2, op2=2`) was handled via special-casing rather than declarative descriptor mapping.
- **Severity:** Medium.

### 2.3 PSTATE Representation, NZCV Flags, DAIF Masks
- **Prior State:** PSTATE flags were maintained in separate fields (`pstate_n_`, `pstate_z_`, `pstate_c_`, `pstate_v_`, `pstate_daif_`, `pstate_spsel_`).
- **Finding:** While flag packing and unpacking worked for exception entry, access via DAIF/NZCV system register aliases lacked strict privilege gating when accessed from EL0.
- **Severity:** Medium.

### 2.4 EL1 System Registers (SPSR, ELR, ESR, FAR, VBAR, SCTLR, TCR, TTBR0/1, MAIR)
- **Prior State:** Architectural registers for EL1 exception handling and MMU configuration were present in `CPU` and `SmpCoreState`.
- **Finding:** Gaps existed in validating illegal access attempts from EL0 (which must trigger an Undefined Instruction exception to EL1 with EC=0x00).
- **Severity:** High (Privilege Isolation).

### 2.5 Thread Pointer Registers (TPIDR_EL0, TPIDR_EL1, TPIDRRO_EL0)
- **Prior State:** Absent from CPU state, SMP core state, and snapshot serialization.
- **Finding:** User-space thread runtime (pthread / TLS) relies fundamentally on `TPIDR_EL0` (read/write at EL0) and `TPIDRRO_EL0` (read-only at EL0, writeable at EL1). Kernel thread pointer `TPIDR_EL1` was unmodeled.
- **Severity:** High (Conformance & OS Readiness).

### 2.6 Instruction Encoding Coverage (op0=3, op1, CRn, CRm, op2)
- **Prior State:** Encodings with `op0 != 3` or unrecognized `CRn/CRm` combinations lacked formal rejection logging and synchronous exception generation.
- **Finding:** Risk of undefined instructions being treated as silent NOPs.
- **Severity:** High.

### 2.7 Privilege Checks on System Register Access (EL0 vs EL1)
- **Prior State:** Privilege was checked ad-hoc in isolated switch branches.
- **Finding:** Lack of unified policy enforcement: EL0 reading an EL1 register did not consistently synthesize `EC=0x00` synchronous trap.
- **Severity:** High.

### 2.8 Unimplemented / Invalid Register Access Behavior
- **Prior State:** Some unknown registers returned 0 and logged a warning without trapping.
- **Finding:** Violates Armv8-A architecture specification; unknown MSR/MRS encodings must fault to EL1 as Undefined Instruction.
- **Severity:** High.

### 2.9 Architectural Register Aliases & Derived State Consistency
- **Prior State:** `CurrentEL` register and `pstate_` fields existed concurrently with risk of divergence if modified outside accessor methods.
- **Severity:** Medium.

### 2.10 Wn vs Xn Register Semantics & 32-bit Zero Extension
- **Prior State:** Most 32-bit arithmetic instructions explicitly truncated to `uint32_t` and cast to `uint64_t`.
- **Finding:** Need exhaustive test verification to guarantee upper 32 bits [63:32] are strictly zeroed upon any 32-bit register write.
- **Severity:** Low.

### 2.11 R31 Encoding (SP vs XZR) Across Instruction Classes
- **Prior State:** Handled in instruction decoder, but needed formal matrix testing across Data Processing (XZR), Load/Store (SP base, XZR transfer), and Branch instructions.
- **Severity:** Low.

### 2.12 PC Advancement & Branch Target Calculation
- **Prior State:** Verified in basic tests; 4-byte alignment, 64-bit wraparound, and conditional branch evaluations were tested.
- **Severity:** Low.

### 2.13 Exception Entry State Capture & ERET State Restoration
- **Prior State:** Exception entry captures `ELR_EL1` and `SPSR_EL1`.
- **Finding:** ERET did not strictly validate whether restoring an illegal execution state (e.g. invalid M[4:0] bits) sets `PSTATE.IL` and generates an Illegal Execution State exception (`EC=0x0E`).
- **Severity:** Medium.

### 2.14 Barriers (DMB, DSB, ISB) & System Instructions (WFI, WFE, SEV, SEVL)
- **Prior State:** Treated as NOPs in single-core execution without formal event-latch synchronization or pipeline stall hooks.
- **Finding:** In multi-core deterministic SMP, WFE/SEV must operate on deterministic event latches without querying host clocks.
- **Severity:** Medium.

### 2.15 Snapshot Serialization / Restoration Coverage
- **Prior State:** `serialize_core_architectural_state` serialized 1056 bytes per core (PC, SP, PSTATE, 31 GPRs, V0-V31, SPSR, ELR, ESR, FAR, VBAR, SCTLR, TCR, TTBR0/1, MAIR).
- **Finding:** Omitted `TPIDR_EL0`, `TPIDR_EL1`, `TPIDRRO_EL0` (24 bytes).
- **Severity:** High.

### 2.16 Canonical / Extended Cluster Hash Inclusion
- **Prior State:** Hashing computed over `SmpCoreState` omitted thread pointer registers.
- **Severity:** High.

---

## 3. Remediation Matrix

| Subsystem | Audit Status | Stage 36 Action | Verification Method |
| :--- | :--- | :--- | :--- |
| System Register Registry | Missing | Implement `sysreg.h` / `sysreg.cpp` with 121 descriptors | `SysRegDescriptorTable` unit test |
| Thread Pointer Registers | Missing | Add `TPIDR_EL0`, `TPIDR_EL1`, `TPIDRRO_EL0` to `CPU` | `TpidrRegisters` unit test |
| Privilege Enforcement | Incomplete | Enforce strict EL0/EL1 checks, route `EC=0x00` | `PrivilegeEnforcement` unit test |
| 32-bit Zero Extension | Verified | Formalize and test `Wn -> Xn[63:32] == 0` invariant | `WxRegisterSemantics` unit test |
| R31 SP vs XZR | Verified | Comprehensive verification across instruction families | `R31SpVsXzr` unit test |
| Exception Entry / ERET | Partial | Implement `PSTATE.IL` trapping on illegal ERET | `ExceptionEntryAndEret` unit test |
| Barriers & Sys-Insts | Incomplete | Formalize deterministic execution semantics | `BarriersAndSystemInstructions` unit test |
| Snapshot & Canonical Hash | Incomplete | Expand core state to 1080 bytes with backward compatibility | `SnapshotAndHashInvariants` unit test |
| Reference Model | Partial | Implement `ReferenceSysRegModel` & tri-way parity | Tri-way comparison test |
