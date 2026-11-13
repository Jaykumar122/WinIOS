# CAMPAIGN 193 — STAGE 193.5 ERET AND EXCEPTION RETURN

- **Campaign:** 193 (ARM64 Exception Architecture, EL0/EL1 Privilege Transitions & Synthetic Kernel Execution)
- **Stage:** 193.5
- **Baseline Git Commit:** `9ff79b3`
- **Host Platform:** Windows 11 x64 (MSVC C++20, `/W4 /WX /permissive-`)
- **Target Device Identity:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 `T8120`)
- **Status:** **COMPLETED — VERIFIED**

---

## 1. OBJECTIVE & ARCHITECTURAL REQUIREMENTS

Stage 193.5 validates the architectural exception return mechanism via `ERET`:
1. **PSTATE Restoration:**
   - Restores condition flags (NZCV), interrupt masks (DAIF), and execution state from `SPSR_EL1`.
2. **PC Restoration:**
   - Restores program counter from `ELR_EL1` (which for `SVC` points to the following instruction, or preferred return address).
3. **Stack Pointer Switching:**
   - Switches between `SP_EL1` (used during kernel exception handling) and `SP_EL0` (restored for user code execution upon returning to EL0).
4. **Privilege Transition:**
   - Transitions CPU execution level from EL1 down to EL0 with `SPSel = 0`.
5. **Illegal Exception Return Handling:**
   - Rejects `ERET` at EL0 as `UndefinedInstruction` (EC 0x00).
   - In accordance with ARMv8-A D1.10.3, if an `ERET` at EL1 has an illegal SPSR target (e.g. `target_el > current_el` or invalid reserved mode), PSTATE.IL is asserted (`il_ = true`).
   - The subsequent instruction fetch at the return target PC triggers an `Illegal Execution State` synchronous exception (`EC = 0x0E`).
6. **Complete Round-Trip Execution:**
   - Validates that a guest program at EL0 triggers `SVC`, enters the EL1 exception vector, executes handler instructions, executes `ERET`, and resumes EL0 guest code execution seamlessly.

---

## 2. IMPLEMENTATION & VERIFICATION

Test target `tests/unit/stage193_5_eret_return_tests.cpp` was implemented and registered in `CMakeLists.txt`.

### Test Cases:
1. `test_complete_roundtrip_el0_to_el1_and_eret_to_el0`:
   - Guest EL0 code (`MOV X0, #42`, `SVC #0`, `ADD X0, X0, #1`, `NOP`).
   - Vector handler at `VBAR_EL1 + 0x400` (`ADD X0, X0, #100`, `ERET`).
   - Step-by-step guest execution confirms `X0` becomes 42 at EL0, then 142 in EL1 handler, returns to EL0, and increments to 143 and completes.
2. `test_pstate_and_flags_preservation`:
   - EL0 sets `NZCV = 0xA` and `DAIF = 0x0`.
   - On exception entry, `DAIF` is masked (`0xF`) and flags are captured in `SPSR_EL1`.
   - Handler mutates CPU flags and unmasks interrupts.
   - Upon `ERET`, CPU restores original `NZCV = 0xA` and `DAIF = 0x0` from `SPSR_EL1`.
3. `test_stack_pointer_isolation_and_restoration`:
   - Verifies isolation of `SP_EL0` (`0x7FFFF000`) and `SP_EL1` (`0xFFFF80000000`).
   - Handler modifies `SP_EL1` to `0xFFFF800000C0`.
   - `ERET` restores `SP_EL0 = 0x7FFFF000` while keeping `SP_EL1` safely preserved.
4. `test_el0_eret_privilege_rejection`:
   - Guest at EL0 executes `ERET`.
   - Traps to EL1 Lower EL Sync vector with `UndefinedInstruction` (EC 0x00) and `ELR_EL1` pointing to the faulting instruction.
5. `test_illegal_exception_return_states`:
   - Tests `ERET` with `SPSR_EL1` targeting EL2h (`M = 0x09`), asserting `PSTATE.IL = 1`. Next fetch causes `IllegalExecutionState` (EC 0x0E).
   - Tests `ERET` with invalid reserved mode (`M = 0x02`), asserting `PSTATE.IL = 1`. Next fetch causes `IllegalExecutionState` (EC 0x0E).

### Execution Verification:
- **Release:** 5/5 passed.
- **Debug:** 5/5 passed.
