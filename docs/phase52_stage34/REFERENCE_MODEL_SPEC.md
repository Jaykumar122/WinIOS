# WinIOS — Independent Reference Model Specification
## Isolated Reference Semantics, Architecture & Workload Coverage

**Project:** WinIOS  
**Phase:** 52, Stage 34  
**Date:** November 2026  
**Module:** `src/reference/independent_reference_model.h` & `.cpp`

---

## 1. Architectural Independence Guarantee

To prevent shared-defect confirmation bias, the Stage 34 reference execution model is strictly isolated from production execution routines:
- **No Production Calls:** Does NOT call or link with `CPU::step()`, `SmpScheduler::step()`, `VirtualDmaEngine::execute_descriptor_chain()`, or production memory access helpers.
- **Isolated Data Structures:** Operates on `ReferenceCpuState`, `ReferenceMemory`, `ReferenceExclusiveMonitor`, and `ReferenceVirtualTimer`.
- **First-Principles Arithmetic & Logic:** All arithmetic carry, borrow, overflow, condition code evaluation, and bitfield extractions are calculated independently from ARMv8-A architecture manual specifications.

---

## 2. Supported Workload Domains

The reference model provides independent execution semantics for:

| Category | Supported Opcodes / Features | Independent Semantic Rules |
|---|---|---|
| **Arithmetic** | `ADD`, `ADDS`, `SUB`, `SUBS` (imm12 & reg, 32/64-bit) | Evaluates carry-out $C$ and signed overflow $V$ via independent bitwise XOR invariants: $V = (op_1 \oplus op_2) \wedge (op_1 \oplus res)$ |
| **Logic** | `AND`, `ANDS`, `ORR`, `EOR`, `BIC` (32/64-bit) | Computes bitwise operations; `ANDS` sets $N$ and $Z$ flags while clearing $C$ and $V$ |
| **Branches** | `B` (imm26), `B.cond` (imm19), `CBZ`, `CBNZ` | Decodes 2's complement displacements; evaluates all 16 condition codes against NZCV |
| **Load / Store** | `LDR`, `STR` (imm12 unsigned offset, 32/64-bit) | Computes scaled offset; accesses `ReferenceMemory`; invalidates exclusive reservations on store |
| **Exclusive Monitor** | `LDXR`, `STXR` (32/64-bit) | Tracks per-core reservation address and size; validates reservation on store; clears on conflict |
| **Barriers** | `DMB`, `DSB`, `ISB` | Handled as architectural memory barrier no-ops in deterministic single-thread scheduler |
| **Virtual Timers** | `CNTVCT_EL0`, `CNTV_CVAL_EL0`, `CNTV_CTL_EL0` | Steps virtual cycles; asserts PPI 27 when $CNTVCT \ge CVAL$ with timer enabled and unmasked |
| **Virtual DMA** | Memcpy, Fill, Chained Descriptors | Parses 64-byte descriptors from `ReferenceMemory`; validates bounds; invalidates cluster monitors |

---

## 3. Field-by-Field Differential Diagnostics

`DifferentialValidator` computes exact delta reports when comparing reference model states against production execution and replay states:
- **`compare_cpu_state`:** Compares $X0..X30$, $PC$, $NZCV$, and total retirement counts.
- **`compare_memory`:** Performs byte-by-byte address scanning across guest RAM, identifying the exact physical address of first divergence.
- **`FirstDivergenceReport`:** Emits structured field name, expected value, actual value, and core ID upon the first mismatch.
