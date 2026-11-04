# WinIOS Phase 52, Stage 19 — State Verification Engine and Canonical Hashing

## 1. Overview

Deterministic replay requires continuous, bit-for-bit verification of architectural state without contamination from host-specific pointers, virtual memory layouts, padding bytes, or host timestamps.

`ReplayStateHash` implements a deterministic, multi-level canonical state hashing algorithm based on Castagnoli CRC32C (polynomial `0x82F63B78`) combined with a 64-bit mixer:

$$\text{CombinedHash} = (\text{CRC}_1 \ll 32) \mid \text{CRC}_2$$

---

## 2. Component Hash Definitions

Architectural state is segmented into six isolated component hashes:

```
+-----------------------------------------------------------------------------------+
|                            ReplayComponentHashes                                  |
+-----------------------------------------------------------------------------------+
|  1. cpu_registers_hash     : Integer Registers X0..X30, SP_EL0..EL3, PC, NZCV, DAIF|
|  2. simd_registers_hash    : 128-bit Vector Registers V0..V31, FPCR, FPSR         |
|  3. system_registers_hash  : VBAR, ELR, ESR, SPSR, SCTLR, HCR, SCR, CPTR, MDCR, TCR|
|  4. memory_hash            : Whole Guest RAM contents                             |
|  5. timer_hash             : Physical & Virtual Counter values, CVAL, Control     |
|  6. gic_hash               : GICv3 Distributor/Redistributor status, priority      |
|  7. combined_hash          : Non-associative 64-bit mixer across components 1..6  |
+-----------------------------------------------------------------------------------+
```

### 2.1 Integer and General-Purpose Registers (`cpu_registers_hash`)

Serialized in strict canonical little-endian byte ordering:
1. $X_0 \dots X_{30}$ (31 registers $\times$ 8 bytes = 248 bytes)
2. Banked Stack Pointers: $SP\_EL0, SP\_EL1, SP\_EL2, SP\_EL3$ (32 bytes)
3. Program Counter: $PC$ (8 bytes)
4. Flags & Control: $NZCV$ (1 byte), $DAIF$ (1 byte), $CurrentEL$ (1 byte), $SPSel$ (1 byte)
- **Host Independence**: No host memory pointers, host registers, or thread IDs are serialized.

### 2.2 SIMD and Floating-Point Registers (`simd_registers_hash`)

1. $V_0 \dots V_{31}$ (32 vector registers $\times$ 16 bytes = 512 bytes, serialized as lower 64-bit word then upper 64-bit word).
2. Floating-Point Status and Control: $FPCR$ (4 bytes), $FPSR$ (4 bytes).

### 2.3 System Registers (`system_registers_hash`)

Includes all architecturally observable system registers:
- Vector base: $VBAR\_EL1, VBAR\_EL2, VBAR\_EL3$
- Exception link: $ELR\_EL1, ELR\_EL2, ELR\_EL3$
- Exception syndrome: $ESR\_EL1, ESR\_EL2, ESR\_EL3$
- Saved status: $SPSR\_EL1, SPSR\_EL2, SPSR\_EL3$
- Control & Hypervisor: $SCTLR\_EL1, HCR\_EL2, SCR\_EL3, CPTR\_EL3, MDCR\_EL3, MDCR\_EL2$
- Translation & Memory: $TTBR0\_EL1, TTBR1\_EL1, TCR\_EL1, MAIR\_EL1$

### 2.4 Guest RAM Memory (`memory_hash`)

- Direct linear Castagnoli CRC32C over the physical memory buffer `memory.raw_data()` of length `memory.size()`.
- Guaranteed identical regardless of host memory allocation address.

### 2.5 Timers and GIC State (`timer_hash`, `gic_hash`)

- Timers: Virtual and physical counter offsets, CVAL match values, and control flags (enable, imask, istatus).
- GICv3: Distributor enable status, pending priority levels, interrupt routing masks.

---

## 3. Checkpoint Tracking and Drift Detection

`ReplayCheckpointManager` records periodic and boundary checkpoints:
- **Interval Checkpoints**: Recorded every $N$ instructions (default: 100,000 instructions).
- **Boundary Checkpoints**: Forced at baseline initialization ($R=0$) and replay completion ($R=R_{\text{target}}$).
- **Diff Generation**: When an unexpected state occurs, `ReplayStateHash::compare_and_diff(expected, actual, diff_desc)` isolates the exact mismatched component (e.g. "Guest RAM hash mismatch: expected 0x... vs actual 0x...") rather than a generic error code.
