# CAMPAIGN 193 — STAGE 193.3 EXCEPTION SYNDROME REGISTER

- **Campaign:** 193 (ARM64 Exception Architecture, EL0/EL1 Privilege Transitions & Synthetic Kernel Execution)
- **Stage:** 193.3
- **Baseline Git Commit:** `9ff79b3`
- **Host Platform:** Windows 11 x64 (MSVC C++20, `/W4 /WX /permissive-`)
- **Target Device Identity:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 `T8120`)
- **Status:** **COMPLETED — VERIFIED**

---

## 1. OBJECTIVE & ARCHITECTURAL REQUIREMENTS

Stage 193.3 implements a structured exception syndrome model and system-register accessibility for exception diagnostics:
1. **Architectural Syndrome Fields:**
   - Exception Class (EC) [bits 31:26]
   - Instruction Length (IL) [bit 25] (1 for 32-bit A64 instruction)
   - Instruction Specific Syndrome (ISS) [bits 24:0] (25 bits, preserving ISV bit 24)
   - Data Fault Status Code (DFSC) [bits 5:0 of ISS for Data Abort]
   - Instruction Fault Status Code (IFSC) [bits 5:0 of ISS for Instruction Abort]
   - Write/Not-Write information (WnR) [bit 6 of ISS for Data Abort]
   - Access-size information (SAS) [bits 23:22 of ISS for Data Abort]
   - Register information (SRT) [bits 20:16 of ISS for Data Abort]
   - Instruction Syndrome Valid (ISV) [bit 24 of ISS for Data Abort]
2. **Guest-Visible System Registers:**
   - `ESR_EL1`: Exception Syndrome Register (EL1)
   - `FAR_EL1`: Fault Address Register (EL1)
   - `ELR_EL1`: Exception Link Register (EL1)
   - `SPSR_EL1`: Saved Program Status Register (EL1)
3. **Privilege Enforcement:**
   - EL1 MRS/MSR instructions execute successfully and update/read the architected registers.
   - EL0 attempts to execute MRS/MSR on `ESR_EL1`, `FAR_EL1`, `ELR_EL1`, or `SPSR_EL1` strictly trap to EL1 with `UndefinedInstruction` (EC 0x00).

---

## 2. IMPLEMENTATION & DEFECT REMEDIATION

### Defect Identified: 24-bit vs 25-bit ISS Mask
- In `src/cpu/synchronous_exception.h`, line 90 previously masked `iss` with `0x00FFFFFFu` (24 bits).
- In ARMv8-A D13.2.37 (ESR_ELx), ISS occupies bits [24:0] (25 bits total). Bit 24 is the critical `ISV` (Instruction Syndrome Valid) flag for Data Aborts.
- Remediation: Updated the ISS mask to `0x01FFFFFFu`.
- Enhanced `make_data_abort` to support parameters `bool isv = false, unsigned srt = 0`. When `isv` is true, bit 24 is asserted, `sas` is encoded in bits [23:22], and `srt` is encoded in bits [20:16].

---

## 3. VERIFICATION & TEST RESULTS

Unit test suite `tests/unit/stage193_3_syndrome_register_tests.cpp` was implemented and registered in `CMakeLists.txt`.

### Test Cases:
1. `test_data_abort_syndrome_fields`:
   - Validates Data Abort syndrome bit fields: EC = 0x24 (Lower EL) or 0x25 (Same EL), IL = 1, ISV = 1, SAS = 3 (8 bytes), SSE = 0, SRT = 1, SF = 1, AR = 0, WnR = 1, DFSC = 0x0D (Permission fault level 1).
   - Validates that FAR holds the exact target fault address (`0x00007FFF10002000ULL`).
2. `test_instruction_abort_syndrome_fields`:
   - Validates Instruction Abort syndrome fields: EC = 0x20 / 0x21, IL = 1, IFSC = 0x0E (Permission fault level 2), FAR holds faulting PC (`0x0000000000401008ULL`).
3. `test_guest_sysreg_access_el1`:
   - Tests guest instructions at EL1 executing `MRS` and `MSR` for `ESR_EL1`, `FAR_EL1`, `ELR_EL1`, and `SPSR_EL1`.
   - Confirms register read/write fidelity and value preservation.
4. `test_rejection_at_el0`:
   - Tests guest instructions at EL0 executing `MRS X0, ESR_EL1`.
   - Confirms that execution triggers an architectural trap to EL1 Lower EL Synchronous vector with `UndefinedInstruction` (EC 0x00).

### Execution Verification:
- **Release:** 4/4 passed.
- **Debug:** Verified via task-52892.
