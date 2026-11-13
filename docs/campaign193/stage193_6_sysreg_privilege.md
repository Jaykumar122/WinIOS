# CAMPAIGN 193 — STAGE 193.6 SYSTEM REGISTER ACCESS AND PRIVILEGE ENFORCEMENT

- **Campaign:** 193 (ARM64 Exception Architecture, EL0/EL1 Privilege Transitions & Synthetic Kernel Execution)
- **Stage:** 193.6
- **Baseline Git Commit:** `9ff79b3`
- **Host Platform:** Windows 11 x64 (MSVC C++20, `/W4 /WX /permissive-`)
- **Target Device Identity:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 `T8120`)
- **Status:** **COMPLETED — VERIFIED**

---

## 1. OBJECTIVE & ARCHITECTURAL REQUIREMENTS

Stage 193.6 audits, validates, and enforces privilege boundaries across the key architectural system registers:
- `CurrentEL`
- `VBAR_EL1`
- `ESR_EL1`
- `ELR_EL1`
- `SPSR_EL1`
- `FAR_EL1`
- `SCTLR_EL1`
- `TTBR0_EL1`
- `TTBR1_EL1`
- `TCR_EL1`
- `MAIR_EL1`
- `TPIDR_EL0`
- `TPIDR_EL1`
- `SP_EL0`
- `SPSel`

Requirements:
1. **EL0 Access Validation:**
   - Registers defined with `min_el = 1` must strictly reject any MRS/MSR attempts at EL0.
   - When unauthorized access occurs with architectural exceptions enabled, the CPU traps to EL1 Lower EL Synchronous vector (`VBAR_EL1 + 0x400`) with `UndefinedInstruction` (EC 0x00).
   - `TPIDR_EL0` is explicitly permitted at EL0 for both read and write.
2. **EL1 Access Validation:**
   - All 15 system registers must be accessible at EL1.
   - Read/write registers must faithfully store and return values.
   - Read-only registers (specifically `CurrentEL`) must reject write attempts (`MSR CurrentEL, Xn` traps or generates undefined instruction exception).
3. **Descriptor Metadata Verification:**
   - Both `find_descriptor_by_id` and `find_descriptor_by_name` in `winios::sysreg` must report correct `min_el`, `is_readable`, and `is_writable` properties matching ARMv8-A specifications.

---

## 2. IMPLEMENTATION & VERIFICATION

Test target `tests/unit/stage193_6_sysreg_privilege_tests.cpp` was implemented and registered in `CMakeLists.txt`.

### Test Cases:
1. `test_sysreg_descriptors_metadata`:
   - Validates metadata for all 15 system registers via `find_descriptor_by_id` and `find_descriptor_by_name`.
   - Confirms `CurrentEL` is read-only at EL1+ (`min_el = 1`), `TPIDR_EL0` is read/write at EL0 (`min_el = 0`), and the remaining 13 are read/write at EL1+ (`min_el = 1`).
2. `test_el1_guest_read_write_access`:
   - Executes guest instructions at EL1 for each register (`MSR` followed by `MRS`), verifying round-trip data preservation for `VBAR_EL1`, `ESR_EL1`, `ELR_EL1`, `SPSR_EL1`, `FAR_EL1`, `SCTLR_EL1`, `TTBR0_EL1`, `TTBR1_EL1`, `TCR_EL1`, `MAIR_EL1`, `TPIDR_EL0`, `TPIDR_EL1`, and `SP_EL0`.
   - Executes `MRS X1, CurrentEL` at EL1, confirming readback of `0x4` (EL1).
3. `test_el0_guest_read_write_privilege_rejection`:
   - Tests 27 distinct unauthorized MRS/MSR guest instructions at EL0 across all 14 privileged registers.
   - Confirms 100% of unauthorized accesses trap to `VBAR_EL1 + 0x400` with `ESR_EL1` EC = 0x00 (`UndefinedInstruction`).
4. `test_el0_permitted_access_tpidr_el0`:
   - Tests guest instructions `MSR TPIDR_EL0, X0` and `MRS X1, TPIDR_EL0` at EL0.
   - Confirms execution succeeds at EL0 without trapping and value is preserved.
5. `test_write_rejection_on_readonly_current_el`:
   - Tests `MSR CurrentEL, X0` at EL1.
   - Confirms illegal write traps to Current EL SPx Sync vector (`VBAR_EL1 + 0x200`) with EC = 0x00.

### Execution Verification:
- **Release:** 5/5 passed.
- **Debug:** 5/5 passed.
