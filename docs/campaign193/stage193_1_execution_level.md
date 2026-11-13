# CAMPAIGN 193 — STAGE 193.1: EL0/EL1 EXECUTION-LEVEL ARCHITECTURE

- **Campaign:** 193
- **Stage:** 193.1
- **Status:** **COMPLETED — VERIFIED**
- **Test Target:** `stage193_1_execution_level_tests` (5/5 tests passed in Release & Debug)

---

## 1. OBJECTIVE & IMPLEMENTATION

Stage 193.1 hardened and validated the guest execution-level representation:
1. **Explicit CurrentEL representation:** The CPU maintains an architectural execution level (`current_el_`) constrained to 2 bits (`& 0x3`).
2. **CurrentEL System Register Conformance:**
   - Reading `CurrentEL` at EL1 returns `0x4` (bits [3:2] = 1).
   - Reading `CurrentEL` at EL2 returns `0x8` (bits [3:2] = 2).
   - Reading `CurrentEL` at EL3 returns `0xC` (bits [3:2] = 3).
   - Reading `CurrentEL` at EL0 is rejected with an architectural `UndefinedInstruction` exception (`EC = 0x00`).
   - Fixed `min_el` for `SysRegId::CurrentEL` in `src/cpu/sysreg.cpp` from `0` to `1` in accordance with ARMv8-A DDI 0487 (where `op1=0` registers are strictly inaccessible at EL0).
   - Writing to `CurrentEL` at any EL generates an architectural Undefined Instruction exception.
3. **Privilege Separation Enforcement:**
   - Privileged operations (`ERET`, `MSR SPSel`, `MRS SCTLR_EL1`, `MRS VBAR_EL1`, `TLBI VMALLE1`) executed at EL0 trigger synchronous exceptions targeting `VBAR_EL1 + 0x400` with `EC = 0x00`.
   - The same operations succeed when executed at EL1.
4. **Invalid Execution Levels:**
   - Any attempt to set out-of-range execution levels (`> 3`) is sanitized via architectural bitmasking (`& 0x3`).

---

## 2. TEST EVIDENCE

Target: `stage193_1_execution_level_tests`
- `test_current_el_reads`: **PASS** (EL1 -> 0x4, EL2 -> 0x8, EL3 -> 0xC).
- `test_current_el_el0_rejection`: **PASS** (traps to EL1 Lower EL Sync vector with EC=0x00).
- `test_privileged_instruction_enforcement`: **PASS** (`ERET`, `SPSel`, `SCTLR_EL1`, `VBAR_EL1`, `TLBI` trap at EL0).
- `test_privileged_instruction_el1_success`: **PASS** (operations execute successfully at EL1).
- `test_invalid_el_sanitization`: **PASS** (masking to 2 bits).

Both Release and Debug builds clean and tests 5/5 passed.
