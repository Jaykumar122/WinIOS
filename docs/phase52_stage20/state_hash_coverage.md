# Architectural State Hash Coverage & Host Immunity

## 1. Objectives of State Hashing

The `ReplayStateHash` subsystem produces a 64-bit cryptographic digest representing the total architectural state of the virtual machine at any given instruction retirement boundary. It serves two vital functions:
1. **Periodic Checkpoint Verification**: Proves that replayed execution matches the recorded execution at intervals (e.g. every 100,000 instructions).
2. **Divergence Isolation**: When two states disagree, the hashing engine isolates the discrepancy down to the exact register, memory byte, or system controller field.

---

## 2. Canonical State Coverage Matrix

To prevent false divergences caused by compiler layouts, padding bytes, or host pointer addresses, the state hasher operates on a strictly canonicalized byte stream:

| Subsystem | Architectural Fields Covered | Byte Size | Canonical Representation |
|---|---|---|---|
| **CPU Core** | `X0`–`X30` (General Purpose) | $31 \times 8 = 248$ bytes | Little-endian 64-bit integers |
| | `PC` (Program Counter) | 8 bytes | Little-endian 64-bit integer |
| | `SP_EL0`, `SP_EL1`, `SP_EL2` | 24 bytes | Little-endian 64-bit integers |
| | `PSTATE` / `NZCV` flags | 8 bytes | Little-endian 64-bit masked flags |
| **FP / SIMD** | `V0`–`V31` (Vector Registers) | $32 \times 16 = 512$ bytes | Raw 128-bit little-endian byte array |
| | `FPSR`, `FPCR` | 8 bytes | Little-endian 32-bit registers |
| **System Regs** | `SCTLR_EL1`, `TCR_EL1`, `TTBR0_EL1`, `TTBR1_EL1`, `MAIR_EL1`, `VBAR_EL1`, `CPACR_EL1`, `ESR_EL1`, `FAR_EL1` | 72 bytes | Little-endian 64-bit values sorted by architectural encoding |
| **VirtualTime** | Base counter ticks, virtual frequency, virtual ns | 24 bytes | Little-endian 64-bit values |
| **GICv3** | Distributor CTLR, Redistributor CTLR, PMR, Pending/Active bitsets | 64 bytes | Little-endian bitmaps in fixed index order |
| **PMU** | Cycle counter, event counters, control flags | 40 bytes | Little-endian counter values |
| **RAM Pages** | Allocated and dirtied 4 KB pages | Dynamic | Sorted by physical base address, hashed chunk-by-chunk |

---

## 3. Host Pointer Immunity & Zero-Leakage Invariant

A critical defect in naive VM hashing implementations is the inadvertent inclusion of host pointer addresses (such as `this`, page table host node pointers, or C++ vtable pointers). Because modern host operating systems employ Address Space Layout Randomization (ASLR), host pointers change across every execution run and between different host machines.

WinIOS enforces **Host Pointer Immunity**:
1. **Zero Raw Pointers**: No C++ pointer, reference, or object address is ever fed to the hash algorithm.
2. **Zero Struct Padding**: Hashing functions never pass `sizeof(Struct)` directly to hash routines. Every field is extracted, serialized explicitly in little-endian order, and hashed field-by-field.
3. **Canonical Page Ordering**: Guest physical memory is hashed strictly in increasing physical address order ($0x0, 0x1000, 0x2000, \dots$). Host heap allocations backing guest memory do not affect order.

---

## 4. Verification in Test Group I

Test Group I (`test_group_i_hash_coverage` in `tests/unit/replay_hardening_tests.cpp`) verified:
1. **Component Sensitivity**: Modifying a single register (`X0` or `X3`), altering a single byte in guest RAM (`0x2000`), or changing VirtualTime ticks immediately altered the computed hash.
2. **Canonical Invariance**: Running two independent CPU and Memory instances located at distinct host virtual addresses with identical guest state yielded bit-for-bit identical state hashes (`0x...`).
3. **Component Diffing**: Calling `ReplayStateHash::diff()` between two disparate states accurately pinpointed the exact component (e.g. `RegisterMismatch` for `X1` or `MemoryMismatch` for page `0x2000`).
