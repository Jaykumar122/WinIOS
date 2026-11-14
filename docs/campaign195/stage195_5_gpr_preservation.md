# Stage 195.5 — Complete General-Purpose Register Preservation (Test A)

## 1. Executive Summary

Stage 195.5 validated Mandatory Acceptance Test A: complete preservation of all 31 general-purpose registers (`X0–X30`) under asynchronous timer preemption at arbitrary instruction boundaries during live execution.

---

## 2. Workload & Test Implementation

### 2.1 Artifact: `guest_preemption_stress`
- **Source:** `tests/artifacts/src/guest_preemption_stress.S`
- **Binary:** `tests/artifacts/bin/guest_preemption_stress` (50,376 bytes, SHA `a4da230fb845f0126cc1bbaa5c57068d5e55a86bb1847cca3a5d054b47f43817`)
- **Toolchain:** Zig 0.17.0 (`-target aarch64-macos -nostdlib -Wl,-pagezero_size,0`)
- **Protocol:**
  - Registers `X0..X30` are initialized with unique deterministic non-zero values (`X0 = 1`, `X1 = 2`, ..., `X30 = 31`).
  - An arithmetic loop executes 500 rounds keeping all registers live.
  - Virtual timer interrupts (PPI 27) arrive asynchronously at arbitrary instruction boundaries.
  - The vector handler (`_irq_vector_handler`) at `VBAR_EL1 + 0x280` allocates a 256-byte stack frame, preserves `X0..X30`, acknowledges GICv3 `ICC_IAR1_EL1` / `ICC_EOIR1_EL1`, increments preemption counters, restores all 31 registers, and resumes via `ERET`.
  - Upon completing the computation, the task performs rigorous equality checks across all 31 registers.

### 2.3 Empirical Results
- Total Instructions Retired: 17,396 instructions.
- Preemptions Handled: 174 live timer preemptions.
- GPR Integrity: 100% (31/31 registers validated identical to pre-preemption state).
- Evidence Classification: `GUEST_EL1`, `GUEST_INTERRUPT`, `GUEST_PREEMPTION`.
