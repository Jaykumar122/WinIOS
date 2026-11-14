# Stage 195.6 — SIMD and Floating-Point Context Preservation (Test B)

## 1. Executive Summary

Stage 195.6 validated Mandatory Acceptance Test B: complete preservation and isolation of all 32 128-bit SIMD/vector registers (`Q0–Q31`), Floating-Point Control Register (`FPCR`), and Floating-Point Status Register (`FPSR`) across thread context switches and preemptive transitions.

---

## 2. Architectural Verification

- **CPACR_EL1 Enforcement:** Bits [21:20] (`FPEN`) must be set to `0b11` to enable full vector and floating-point operations in EL0 and EL1 without trapping.
- **Extended Storage:** Vector registers `Q0..Q31` (512 bytes) and `FPCR`/`FPSR` are embedded at offsets `0x060–0x067` and `0x170–0x370` within `ExceptionFrame`.
- **Validation Protocol:**
  - Task A populates all 32 vector registers with distinct patterns (`low = 0xA0000000 + i`, `high = 0xB0000000 + i`) and configures `FPCR = 0x01000000`, `FPSR = 0x00000008`.
  - Task A is preempted and saved.
  - Task B executes and overwrites all 32 vector registers with conflicting patterns (`low = 0xC0000000 + i`, `high = 0xD0000000 + i`), `FPCR = 0x02000000`, `FPSR = 0x00000004`.
  - Task A is restored: every single 128-bit vector register and FP status/control word is verified 100% intact.
  - Task B is restored: its vector and FP state is verified intact.

Evidence Classification: `GUEST_EL1`, `GUEST_CONTEXT_SWITCH`, `GUEST_PREEMPTION`.
