# Stage 195.7 — PSTATE, DAIF and Exception-Return Preservation (Test C)

## 1. Executive Summary

Stage 195.7 verified Mandatory Acceptance Test C: preservation and isolation of condition flags (`NZCV`), interrupt masks (`DAIF`), and exception return context (`ELR_EL1`, `SPSR_EL1`) across task preemption.

---

## 2. Invariant Verification

- **Flag Isolation:** Task 1 executes with `NZCV = 0x9` (Negative + Overflow) and `DAIF = 0x2`. Task 2 executes with `NZCV = 0x6` (Zero + Carry) and `DAIF = 0x0`.
- **Exception Return Mechanics:**
  - Exception entry captures PSTATE into `SPSR_EL1` and masks DAIF.
  - The exception frame preserves `SPSR_EL1` at offset `0x018`.
  - Upon task restoration and `ERET`, `SPSR_EL1` is unpacked into `PSTATE`, restoring original condition codes and interrupt masks without leakage between tasks.
- **Results:**
  - Task 1 restored with `NZCV = 0x9`, `DAIF = 0x2`, and original PC intact.
  - Task 2 restored with `NZCV = 0x6`, `DAIF = 0x0`, and original PC intact.

Evidence Classification: `GUEST_EXCEPTION`, `GUEST_PREEMPTION`.
