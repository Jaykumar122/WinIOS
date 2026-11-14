# Stage 196.3 — Atomic Instruction Family Expansion (Acceptance Test B)

## 1. Architectural Implementation & Defect Resolution
During audit of ARMv8.1-A Large System Extensions (LSE) and exclusive atomic instructions, a critical encoding discrepancy was discovered in `CAS` / `CASA` / `CASL` / `CASAL`:
- **Discrepancy:** The previous decoding assumed `kLseCasMask = 0x0F20FC00u` with bit 15 tested as 0, and checked bit 23 for Acquire and bit 22 for Release.
- **Architectural Specification (ARM DDI 0487):**
  - `CAS` encodings: `size(31:30) 001000 1 A 1 Rs L 11111 Rn Rt`
  - Bit 22 is Acquire (`A`)
  - Bit 15 is Release (`L`)
  - Bit 23 is fixed to 1 (`b23 = 1`)
  - Bits [14:10] are fixed to `11111` (`0x1F`)
  - As a result, `CASL` and `CASAL` were previously rejected as illegal instructions, and `CASA` was misidentified.
- **Remediation:**
  - Updated mask to `kLseCasMask = 0x3FA07C00u`, pattern `kLseCasPattern = 0x08A07C00u`.
  - Decoded `acquire = (instruction & (1u << 22u)) != 0`, `release = (instruction & (1u << 15u)) != 0`.
  - Fixed both execution engine and disassembler.

## 2. Supported Atomic Families
1. **Compare-and-Swap:** `CAS`, `CASA`, `CASL`, `CASAL` in 32-bit and 64-bit variants.
2. **Swap:** `SWP`, `SWPA`, `SWPL`, `SWPAL` in 32-bit and 64-bit variants.
3. **Atomic Arithmetic / Logical:** `LDADD`, `LDCLR`, `LDEOR`, `LDSET`, `LDSMAX`, `LDSMIN`, `LDUMAX`, `LDUMIN` with natural alignment and serialized execution.
4. **Exclusive Pairs:** `LDXP`, `STXP`, `LDAXP`, `STLXP` (16-byte pairs).

## 3. Evidence Classification
`GUEST_ATOMIC`, `GUEST_MEMORY_ORDER`.
Tested via `stage196_3_atomic_instructions_tests`.
