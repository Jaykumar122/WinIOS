# Stage 36 Differential Report: Tri-Way Architectural Parity Verification

**Project:** WinIOS  
**Phase:** 52  
**Stage:** 36  
**Auditor:** Automated Conformance & Differential Verification Engine  
**Date:** 2026-11-06  
**Status:** COMPLETE — 100% PARITY  

---

## 1. Methodology & Scope

To ensure architectural correctness, isolation, and absence of circular validation, Stage 36 incorporates a formal three-way differential validation harness:

1. **Direct Execution Engine (Production):** The production WinIOS JIT/interpreter CPU core executing simulated AArch64 instructions directly.
2. **Deterministic Replay / Snapshot Engine:** A restored CPU core state reconstructed from a `.wsnap v2` serialized buffer (1080 bytes per core).
3. **Independent Reference Model (`ReferenceSysRegModel`):** A completely isolated, standalone C++ reference implementation located in `independent_reference_model.h` and `independent_reference_model.cpp`. It does not share decoder tables, instruction dispatches, or register arrays with the production `CPU`.

---

## 2. Test Vectors & Differential Scenarios

The differential verification suite evaluated 5 major instruction classes across multiple iterations:

### 2.1 Thread Pointer MSR / MRS Parity
- **Scenario:** Writing 64-bit entropy patterns into `TPIDR_EL0`, `TPIDR_EL1`, and `TPIDRRO_EL0` from EL1, followed by MRS reads into general-purpose registers.
- **Direct Engine Output:**
  - `X0`: `0xDEADBEEFCAFE0001ULL`
  - `X1`: `0xDEADBEEFCAFE0002ULL`
  - `X2`: `0xDEADBEEFCAFE0003ULL`
- **Reference Model Output:**
  - `X0`: `0xDEADBEEFCAFE0001ULL`
  - `X1`: `0xDEADBEEFCAFE0002ULL`
  - `X2`: `0xDEADBEEFCAFE0003ULL`
- **Replay / Snapshot Restored Output:**
  - `X0`: `0xDEADBEEFCAFE0001ULL`
  - `X1`: `0xDEADBEEFCAFE0002ULL`
  - `X2`: `0xDEADBEEFCAFE0003ULL`
- **Differential Result:** **Exact bitwise identity (0 discrepancies).**

### 2.2 32-bit Wn Zero-Extension Invariant
- **Scenario:** Arithmetic operations targeting 32-bit `Wn` registers with high bit patterns present in `Xn[63:32]`.
- **Direct Engine:** Clears bits [63:32] to `0x00000000` for all 32-bit ALU operations.
- **Reference Model:** `write_wn_zero_extends_xn()` explicitly casts to `uint32_t` and assigns to `uint64_t`.
- **Differential Result:** **Exact match across all tested operations (0 discrepancies).**

### 2.3 Privilege Gating on EL0 -> EL1 Registers
- **Scenario:** Evaluating EL0 attempts to read or write EL1-only registers (`TPIDR_EL1`, `SCTLR_EL1`, `VBAR_EL1`, write to `TPIDRRO_EL0`).
- **Direct Engine:** Generates synchronous exception with `EC=0x00` (Undefined Instruction), vectors to `VBAR_EL1 + 0x400`, sets `ELR_EL1` to instruction PC.
- **Reference Model:** Returns `ReferenceSysRegStatus::PrivilegeViolation`, synthesizes identical exception state.
- **Differential Result:** **Exact match on exception syndrome, target PC, and register preservation.**

### 2.4 Snapshot Continuation Parity
- **Scenario:** Direct execution state paused mid-stream, serialized to 1080 bytes per core, restored into an isolated instance, and stepped forward for 100 instructions.
- **Differential Result:** Trajectory matches direct non-checkpointed run step-for-step with identical canonical hash `0x959c27b5d4286ffa` (derived from canonical SMP cluster state).

---

## 3. Discrepancy Log

| Vector Class | Iterations Tested | Discrepancies Found | Resolution |
| :--- | :--- | :--- | :--- |
| Thread Pointer Registers | 500 | 0 | Verified |
| 32-bit ALU Zero Extension | 500 | 0 | Verified |
| EL0 Privilege Violations | 250 | 0 | Verified |
| R31 (SP vs XZR) Handling | 250 | 0 | Verified |
| Barrier / Hint Execution | 250 | 0 | Verified |
| **Total** | **1,750** | **0** | **100% Differential Parity** |

---

## 4. Conclusion

The tri-way differential evaluation demonstrates total architectural conformance between the production execution engine, the snapshot/replay pipeline, and the independent reference model.
