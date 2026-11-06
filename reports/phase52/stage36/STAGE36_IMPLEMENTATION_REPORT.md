# Stage 36 Implementation Report: System Register Architecture, Privilege Enforcement & CPU Execution Conformance

**Project:** WinIOS  
**Phase:** 52  
**Stage:** 36  
**Implementation Lead:** Architectural Conformance Engineer  
**Date:** 2026-11-06  
**Status:** COMPLETE  

---

## 1. Architectural Overview

Stage 36 introduces a formal System Register Architecture, strict privilege checking, complete thread-pointer register modeling (`TPIDR_EL0`, `TPIDR_EL1`, `TPIDRRO_EL0`), deterministic barrier/hint execution, snapshot/state extension (1080 bytes per core), and an independent reference model (`ReferenceSysRegModel`).

---

## 2. Implemented Components & Mechanics

### 2.1 Centralized System Register Registry (`sysreg.h`, `sysreg.cpp`)
- **Key Encoding Scheme:**
  Armv8-A MRS/MSR encodings use five fields: `op0` (2 bits), `op1` (3 bits), `CRn` (4 bits), `CRm` (4 bits), `op2` (3 bits). We map these into a packed 16-bit key:
  $$\text{key} = ((\text{op0} == 2 \;?\; 0 : 1) \ll 14) \mid ((\text{op1} \ \&\ 7) \ll 11) \mid ((\text{CRn} \ \&\ 15) \ll 7) \mid ((\text{CRm} \ \&\ 15) \ll 3) \mid (\text{op2} \ \&\ 7)$$
- **SysRegDescriptor Structure:**
  - `SysRegId id`: Typed enum identifier.
  - `uint16_t key`: Packed 16-bit encoding key.
  - `const char* name`: Standard Armv8-A architectural mnemonic.
  - `uint8_t op0, op1, CRn, CRm, op2`: Raw architectural bitfields.
  - `PrivilegeLevel min_el`: Minimum privilege required (`EL0`, `EL1`, `EL2`, `EL3`).
  - `RegisterAccess permissions`: Read-only, Write-only, Read-Write.
  - `uint64_t reset_value`: Canonical architectural reset value.
  - `uint64_t write_mask`: Implemented writable bitmask.
  - `uint64_t res0_mask`, `uint64_t res1_mask`: Reserved bit semantics.
- **Coverage:** 121 registered descriptors covering:
  - Identification & Feature registers (MIDR_EL1, MPIDR_EL1, ID_AA64PFR0_EL1, etc.).
  - Exception and control registers (CurrentEL, SPSR_EL1, ELR_EL1, ESR_EL1, FAR_EL1, VBAR_EL1, SCTLR_EL1, CPACR_EL1).
  - Memory system and MMU (TCR_EL1, TTBR0_EL1, TTBR1_EL1, MAIR_EL1, PAR_EL1).
  - Thread pointer registers (TPIDR_EL0, TPIDR_EL1, TPIDRRO_EL0).
  - Timers and Counters (CNTV_CTL_EL0, CNTV_CVAL_EL0, CNTV_TVAL_EL0, CNTPCT_EL0, etc.).
  - GICv3 CPU interface registers (ICC_PMR_EL1, ICC_IAR1_EL1, ICC_EOIR1_EL1, ICC_SRE_EL1, etc.).

### 2.2 Thread Pointer System Registers in CPU Core
- Added member variables to `CPU` (`cpu.h`):
  - `uint64_t tpidr_el0_`: User-space read/write thread pointer.
  - `uint64_t tpidr_el1_`: Privileged EL1 kernel thread pointer.
  - `uint64_t tpidrro_el0_`: User-space read-only thread pointer (kernel writeable).
- Added accessors:
  - `uint64_t tpidr_el0() const`, `void set_tpidr_el0(uint64_t val)`.
  - `uint64_t tpidr_el1() const`, `void set_tpidr_el1(uint64_t val)`.
  - `uint64_t tpidrro_el0() const`, `void set_tpidrro_el0(uint64_t val)`.
- Initialization: Reset to `0x0ULL` in `CPU::reset()`.

### 2.3 Strict Privilege Enforcement
- Architectural Rule:
  1. If current execution level $\text{EL} < \text{min\_el}$, the access is illegal.
  2. If the operation is a write on a Read-Only register, or a read on a Write-Only register, the access is illegal.
  3. When an illegal access occurs from EL0:
     - WinIOS routes an Undefined Instruction exception to EL1.
     - Exception Class (`EC`) = `0x00`.
     - `ELR_EL1` captures the address of the faulting MRS/MSR instruction.
     - `PSTATE` is saved to `SPSR_EL1`, DAIF masked, and execution vectors to `VBAR_EL1 + 0x400` (Lower EL using AArch64).
  4. Specific Register Policies:
     - `TPIDR_EL0`: Accessible (R/W) at EL0 and EL1.
     - `TPIDRRO_EL0`: Read-Only at EL0; Read/Write at EL1. EL0 MSR attempts generate `EC=0x00` Undefined Instruction.
     - `TPIDR_EL1`: EL1-only. EL0 MRS or MSR attempts generate `EC=0x00` Undefined Instruction.

### 2.4 Register Semantics & Execution Invariants
- **Wx Zero Extension:** Any write to a 32-bit destination register `Wn` zeroes bits [63:32] of the 64-bit architectural register `Xn`. Formally verified across arithmetic, logical, and shift operations.
- **R31 SP vs XZR:** Verified across instruction groups:
  - Data-processing (reg/imm): R31 denotes `XZR` (reads zero, writes discarded).
  - Add/Subtract (extended/shifted): R31 denotes `SP` if used as base/destination.
  - Load/Store: R31 base denotes `SP`; R31 transfer register denotes `XZR`.
- **Condition Flags (NZCV):** Verified across all 16 condition codes (EQ, NE, CS/HS, CC/LO, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL, NV).
- **Illegal ERET Trapping:** Validated that attempting an ERET with invalid `SPSR_EL1.M[4:0]` (e.g. attempting to return to EL2/EL3 or unsupported execution state) forces `PSTATE.IL` and raises an Illegal Execution State exception (`EC=0x0E`).

### 2.5 Barriers and System Instructions
- `DMB` / `DSB`: Deterministically enforce memory ordering across all simulated virtual cores without host thread stalling.
- `ISB`: Flushes simulated instruction prefetch and synchronizes context.
- `WFI` / `WFE`: Evaluates deterministic virtual interrupt / event latch status. In replay mode, does not block host execution.
- `SEV` / `SEVL`: Sets the multi-core event latch deterministically across all cores in `SmpTopology`.

### 2.6 Snapshot Format Extension (1080 Bytes per Core)
- Updated `serialize_core_architectural_state` and `deserialize_core_architectural_state` in `smp_snapshot_format.cpp`:
  - Bytes [0..1055]: Legacy 1056-byte core state (PC, SP, PSTATE, GPRs, Vector registers, EL1 control regs).
  - Bytes [1056..1063]: `TPIDR_EL0` (uint64_t little-endian).
  - Bytes [1064..1071]: `TPIDR_EL1` (uint64_t little-endian).
  - Bytes [1072..1079]: `TPIDRRO_EL0` (uint64_t little-endian).
  - Total serialized size: **1080 bytes**.
- Backward Compatibility: Deserializer accepts any buffer $\ge 1056$ bytes; if buffer $< 1080$, thread pointer registers default gracefully to zero without corrupting older snapshot versions.
- Canonical Hash: `compute_canonical_hash()` incorporates all 24 bytes of TPIDR state into the 64-bit FNV-1a state digest.

### 2.7 Independent Differential Reference Model
- Implemented `ReferenceSysRegModel` in `independent_reference_model.h` and `independent_reference_model.cpp`.
- Operates on isolated `ReferenceSysRegState` without calling production CPU or emulator modules.
- Independent decoder table, privilege logic, and arithmetic zero-extension implementations.
