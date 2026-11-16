# WinIOS Campaign 201 — Stage 201.5 & 201.6 ARM64 Differential Validation

**Campaign:** 201  
**Stages:** 201.5 (AArch64 Instructions) & 201.6 (System Registers & Exceptions)  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target:** Independent validation of AArch64 instruction semantics and exception architecture against Arm DDI 0487  
**Date:** November 16, 2026  

---

## 1. Executive Summary

Stages 201.5 and 201.6 subjected the WinIOS ARM64 CPU engine to rigorous differential testing against independent architectural oracles derived from the official *Arm Architecture Reference Manual (ARM DDI 0487K.a)* and previous Unicorn Engine 2.1.4 findings.

Key validation results:
1. **Arithmetic & Logic Flag Truth:** All four NZCV condition flags ($N$: Negative, $Z$: Zero, $C$: Carry, $V$: Signed Overflow) generate exact architectural values across critical boundary conditions (`0x7FFFFFFFFFFFFFFF + 1`, `0xFFFFFFFFFFFFFFFF + 1`, unsigned borrow `0 - 1`, and equality comparisons).
2. **Shifted Logical Operands:** Operations combining arithmetic/logical instructions with shifted second operands (`LSL`, `LSR`, `ASR`) execute with bit-level accuracy without corrupting register state.
3. **Bitfield & Reversal Operations:** Validated `UBFM`, `SBFM` (sign extension), `RBIT` (64-bit bit reversal), `REV` (endianness swap), and `CLZ` (count leading zeros) against mathematical reference values.
4. **Condition Code Matrix:** Evaluated all 14 standard AArch64 condition codes (`EQ`, `NE`, `CS`, `CC`, `MI`, `PL`, `VS`, `VC`, `HI`, `LS`, `GE`, `LT`, `GT`, `LE`), verifying exact branch-taken versus fallthrough outcomes based on PSTATE.
5. **System Register Privilege Model:** Accessing EL1 system registers (e.g. `SCTLR_EL1`) from EL0 reliably traps with Exception Class `0x18` (Trapped MSR/MRS) or `0x00`, preventing unprivileged state alteration.
6. **Exception Vector & Return Integrity:** Verified `SVC #imm` delivery to vector offset `+0x400` with `EC = 0x15`, and validated `ERET` state restoration (PSTATE, PC, privilege drops) and illegal target mode detection (`EC = 0x0E`).

---

## 2. Instruction Differential Matrix

| Instruction Class | Test Vector / Operation | Reference Rule (Arm DDI 0487) | WinIOS Observed Result | Status |
| :--- | :--- | :--- | :--- | :--- |
| **ADDS Signed Overflow** | `ADDS X0, X1, #1` with `X1 = 0x7FFFFFFFFFFFFFFF` | Result `0x8000000000000000`, $N=1, Z=0, C=0, V=1$ | Identical match | **PASS** |
| **ADDS Unsigned Wrap** | `ADDS X0, X1, #1` with `X1 = 0xFFFFFFFFFFFFFFFF` | Result `0x0000000000000000`, $N=0, Z=1, C=1, V=0$ | Identical match | **PASS** |
| **SUBS Borrow** | `SUBS X0, X1, #1` with `X1 = 0` | Result `0xFFFFFFFFFFFFFFFF`, $N=1, Z=0, C=0, V=0$ | Identical match | **PASS** |
| **SUBS Equal** | `SUBS X0, X1, X2` with `X1 = X2 = 0x12345678` | Result `0`, $N=0, Z=1, C=1, V=0$ | Identical match | **PASS** |
| **Shifted ANDS** | `ANDS X0, X1, X2, LSL #4` | Masks with shifted operand, updates $N, Z$ | Identical match | **PASS** |
| **Shifted BICS** | `BICS X0, X1, X2, LSR #8` | Bit-clear with shifted mask | Identical match | **PASS** |
| **Shifted EOR** | `EOR X0, X1, X2, ASR #4` | Preserves sign extension in shifted operand | Identical match | **PASS** |
| **UBFM Extraction** | `UBFM X0, X1, #16, #31` | Extracts bits [31:16] into bits [15:0] | Identical match | **PASS** |
| **SBFM Sign-Extend** | `SBFM X0, X1, #0, #15` | Sign-extends 16-bit word to 64-bit | Identical match | **PASS** |
| **RBIT** | `RBIT X0, X1` with bit 0 set | Produces bit 63 set (`0x8000000000000000`) | Identical match | **PASS** |
| **REV** | `REV X0, X1` | Reverses byte order | Identical match | **PASS** |
| **CLZ** | `CLZ X0, X1` with bit 0 set | Returns 63 | Identical match | **PASS** |
| **Condition Codes** | 14 Condition codes evaluated | Exact branch vs fallthrough per NZCV | Identical match | **PASS** |
| **LDRSW** | `LDRSW X0, [X1]` loading `0x80001234` | Sign-extends to `0xFFFFFFFF80001234` | Identical match | **PASS** |
| **STP Pre-Indexed** | `STP X0, X1, [SP, #-16]!` | Decrements SP by 16 before store | Identical match | **PASS** |
| **LDP Post-Indexed** | `LDP X0, X1, [SP], #16` | Increments SP by 16 after load | Identical match | **PASS** |

---

## 3. Exception & Privilege Differential Matrix

| Subsystem / Transition | Architectural Scenario | Expected Behavior | WinIOS Result | Status |
| :--- | :--- | :--- | :--- | :--- |
| **EL0 MRS Gating** | EL0 executing `MRS X0, SCTLR_EL1` | Traps to EL1 with EC `0x18` or `0x00` | Trapped to EL1, EC recorded | **PASS** |
| **EL1 MRS Access** | EL1 executing `MRS X0, SCTLR_EL1` | Unrestricted read of system register | Returned register value | **PASS** |
| **SVC Vector Delivery** | EL0 executing `SVC #0x42` | Enters EL1 at VBAR + `0x400`, `EC = 0x15` | PC = VBAR+0x400, ISS=0x42 | **PASS** |
| **ERET Restoration** | EL1 executing `ERET` with target EL0 | Drops EL to 0, restores PSTATE & PC | EL0 active, flags restored | **PASS** |
| **Illegal ERET** | EL1 `ERET` with invalid SPSR target mode | Traps with `EC = 0x0E` (Illegal Return) | Synchronous exception taken | **PASS** |

---

## 4. Test Suite Reference
- Executable 1: `stage201_5_arm64_instruction_differential_tests`
- Executable 2: `stage201_6_sysreg_exception_differential_tests`
