# WinIOS Phase 52, Stage 17: CPU Architectural State Snapshot Specification

## 1. Scope and Inventory

This document defines the bit-exact serialization format, capability inventory, and restored state guarantees for the WinIOS AArch64 CPU architectural model.

The CPU state payload captures the complete processor context required to resume instruction execution without deviation.

---

## 2. Supported Architectural State Inventory

The `SnapshotSerializer::serialize_cpu()` function captures the following architectural CPU state:

| Architectural Domain | Register / Field | Width | Description |
|---|---|---|---|
| **General Purpose** | `X0` – `X30` | $31 \times 64$ bits | Integer general-purpose registers (including `X30` link register). |
| **Stack Pointers** | `SP_EL0`, `SP_EL1`, `SP_EL2`, `SP_EL3` | $4 \times 64$ bits | Stack pointers for each Exception Level. |
| **Program Counter** | `PC` | 64 bits | Current architectural instruction fetch address. |
| **Current State** | `PSTATE` | 64 bits | Current condition flags (NZCV) and mask flags (DAIF, BTYPE, SS). |
| **Saved State** | `SPSR_EL1`, `SPSR_EL2`, `SPSR_EL3` | $3 \times 64$ bits | Saved Process Status Registers for exception returns (`ERET`). |
| **Exception Link** | `ELR_EL1`, `ELR_EL2`, `ELR_EL3` | $3 \times 64$ bits | Exception Link Registers. |
| **Vector / SIMD** | `V0` – `V31` | $32 \times 128$ bits | NEON/Advanced SIMD 128-bit vector registers. |
| **FP Control/Status**| `FPCR`, `FPSR` | $2 \times 32$ bits | Floating-Point Control Register and Status Register. |
| **Exception Control**| `VBAR_EL1`, `VBAR_EL2`, `VBAR_EL3` | $3 \times 64$ bits | Vector Base Address Registers. |
| **Syndrome / Fault** | `ESR_EL1`, `ESR_EL2`, `ESR_EL3` | $3 \times 64$ bits | Exception Syndrome Registers. |
| **Fault Address** | `FAR_EL1`, `FAR_EL2`, `FAR_EL3` | $3 \times 64$ bits | Fault Address Registers. |
| **Thread Pointers** | `TPIDR_EL0`, `TPIDRRO_EL0`, `TPIDR_EL1`, `TPIDR_EL2`, `TPIDR_EL3` | $5 \times 64$ bits | User and kernel thread context ID registers. |
| **System Control** | `SCTLR_EL1`, `SCTLR_EL2`, `SCTLR_EL3` | $3 \times 64$ bits | System Control Registers (MMU enable, cache enable, alignment). |
| **Translation Base**| `TTBR0_EL1`, `TTBR1_EL1`, `TTBR0_EL2`, `TTBR0_EL3` | $4 \times 64$ bits | Translation Table Base Registers. |
| **Translation Ctrl**| `TCR_EL1`, `TCR_EL2`, `TCR_EL3` | $3 \times 64$ bits | Translation Control Registers. |
| **Memory Attributes**| `MAIR_EL1`, `MAIR_EL2`, `MAIR_EL3` | $3 \times 64$ bits | Memory Attribute Indirection Registers. |
| **Context ID** | `CONTEXTIDR_EL1` | 64 bits | ASID and process identification. |
| **Execution State** | `current_el` | 8 bits | Current active Exception Level (`EL0`, `EL1`, `EL2`, `EL3`). |
| **Security State** | `security_state` | 8 bits | Current Security State (`Secure`, `NonSecure`). |
| **Lifecycle** | `is_started`, `is_stopped` | $2 \times 8$ bits | CPU thread execution state. |
| **Trace Counter** | `instruction_trace_sequence` | 64 bits | Total instructions executed / trace sequence anchor. |

---

## 3. Binary Payload Memory Layout (Exact 1128 Bytes)

The CPU State payload (`SnapshotSectionType::CpuState`) has a fixed binary layout of **exactly 1128 bytes**:

```
+-------------------------------------------------------------------------+
| Byte Range | Field                                                      |
+------------+------------------------------------------------------------+
| 0000..0247 | General Purpose Registers X0..X30 (31 * 8 = 248 bytes)     |
| 0248..0279 | Stack Pointers SP_EL0, SP_EL1, SP_EL2, SP_EL3 (32 bytes)   |
| 0280..0287 | Program Counter PC (8 bytes)                               |
| 0288..0295 | Process State PSTATE (8 bytes)                             |
| 0296..0319 | Saved Process State SPSR_EL1, SPSR_EL2, SPSR_EL3 (24 bytes) |
| 0320..0343 | Exception Link Registers ELR_EL1..EL3 (24 bytes)           |
| 0344..0855 | SIMD / Vector Registers V0..V31 (32 * 16 = 512 bytes)      |
| 0856..0859 | Floating-Point Control Register FPCR (4 bytes)             |
| 0860..0863 | Floating-Point Status Register FPSR (4 bytes)              |
| 0864..0887 | Vector Base Addresses VBAR_EL1..EL3 (24 bytes)             |
| 0888..0911 | Exception Syndrome Registers ESR_EL1..EL3 (24 bytes)       |
| 0912..0935 | Fault Address Registers FAR_EL1..EL3 (24 bytes)            |
| 0936..0975 | Thread Pointer Registers TPIDR_EL0..EL3 (40 bytes)         |
| 0976..0999 | System Control Registers SCTLR_EL1..EL3 (24 bytes)         |
| 1000..1031 | Translation Table Base TTBR0_EL1, TTBR1_EL1, TTBR0_EL2,    |
|            | TTBR0_EL3 (32 bytes)                                       |
| 1032..1055 | Translation Control Registers TCR_EL1..EL3 (24 bytes)      |
| 1056..1079 | Memory Attribute Registers MAIR_EL1..EL3 (24 bytes)        |
| 1080..1087 | Context ID Register CONTEXTIDR_EL1 (8 bytes)               |
| 1088..1088 | Current Exception Level (1 byte)                           |
| 1089..1089 | Current Security State (1 byte)                            |
| 1090..1090 | Lifecycle: is_started (1 byte)                             |
| 1091..1091 | Lifecycle: is_stopped (1 byte)                             |
| 1092..1095 | Alignment Padding (4 bytes, zero-filled)                   |
| 1096..1103 | Instruction Trace Sequence Counter (8 bytes)               |
| 1104..1127 | Reserved Expansion Field (24 bytes, zero-filled)           |
+-------------------------------------------------------------------------+
| TOTAL: 1128 BYTES                                                       |
+-------------------------------------------------------------------------+
```

---

## 4. Associated Architectural Sections

In addition to core integer and sysreg state, the CPU architecture is modularized into dedicated sections:

### 4.1 Performance Monitoring Unit (PMU) State (`Section 5`)
- Payload size: **96 bytes**.
- Encodes:
  - `pmcr` (32 bits), `pmcnten` (32 bits), `pmovs` (32 bits).
  - Cycle Counter `pmccntr` (64 bits).
  - 6 Hardware Event Counters `pmevcntr0`..`pmevcntr5` ($6 \times 64$ bits).
  - 6 Hardware Event Type Selectors `pmevtyper0`..`pmevtyper5` ($6 \times 32$ bits).

### 4.2 Hardware Debug State (`Section 6`)
- Payload size: **144 bytes**.
- Encodes:
  - Debug Control `mdscr_el1` (64 bits).
  - 4 Hardware Breakpoint Value Registers `dbgbvr0`..`dbgbvr3` ($4 \times 64$ bits).
  - 4 Hardware Breakpoint Control Registers `dbgbcr0`..`dbgbcr3` ($4 \times 32$ bits).
  - 4 Hardware Watchpoint Value Registers `dbgwvr0`..`dbgwvr3` ($4 \times 64$ bits).
  - 4 Hardware Watchpoint Control Registers `dbgwcr0`..`dbgwcr3` ($4 \times 32$ bits).

---

## 5. Round-Trip Verification Guarantees

Unit test `test_cpu_state_serialization_roundtrip` asserts bit-exact identity for:
1. Every register $X0 \dots X30$.
2. All 4 stack pointers $SP\_EL0 \dots SP\_EL3$.
3. $PC$, $PSTATE$, and condition flags.
4. All $V0 \dots V31$ 128-bit vectors.
5. All system registers across $EL0, EL1, EL2, EL3$.
6. MMU configuration ($SCTLR, TTBR0/1, TCR, MAIR$).
7. PMU counters and event selector registers.
8. Hardware breakpoints, watchpoints, and debug monitor configuration.

---

## 6. Documented Unsupported State

WinIOS does not implement the following optional ARMv8 features; therefore, they are deliberately omitted from the snapshot format to avoid serializing placeholder or uninitialized data:
- **Scalable Vector Extension (SVE / SVE2)**: Scalable predicate and vector registers (`Z0-Z31`, `P0-P15`).
- **Scalable Matrix Extension (SME)**: Accumulator tile matrices (`ZA`).
- **Pointer Authentication Keys**: `APIAKey`, `APIBKey`, etc.
- **Memory Tagging Extension (MTE)**: Allocation tag granules.
- **Cryptographic Extensions**: Hardware SHA/AES pipeline states outside standard NEON registers.
- **Virtualization EL2 nested virtualization (FEAT_NV)**.
