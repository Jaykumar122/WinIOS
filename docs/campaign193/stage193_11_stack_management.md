# Stage 193.11 — Stack Management and Context Preservation

## 1. Objectives & Scope
Stage 193.11 validates guest stack pointer selection and context preservation:
- Banked stack pointer architecture: `SP_EL0` (user stack pointer) and `SP_EL1` (kernel stack pointer).
- `SPSel` stack selection:
  - At EL0: `SP_EL0` is always used regardless of SPSel.
  - At EL1: `SPSel = 0` selects `SP_EL0` (EL1t); `SPSel = 1` selects `SP_EL1` (EL1h).
- Exception stack switching: taking an exception from EL0 automatically switches stack selection to `SP_EL1`, preserving `SP_EL0`.
- Stack alignment checking (`SCTLR_EL1.SA0` and `SCTLR_EL1.SA`):
  - When enabled, using an unaligned SP as base in load/store instructions generates an `SPAlignmentFault` (EC `0x26`).
  - At EL0: traps to Lower EL Synchronous vector (`VBAR_EL1 + 0x400`).
  - At EL1: traps to Current EL SPx Synchronous vector (`VBAR_EL1 + 0x200`).
- Nested exception handling and context preservation:
  - Outer EL1 handler saves `ELR_EL1` and `SPSR_EL1`.
  - Nested exception enters EL1, updates `ELR_EL1` and `SPSR_EL1`, and executes nested handler.
  - Nested handler returns via `ERET` to outer handler.
  - Outer handler restores user `ELR_EL1` and `SPSR_EL1`, then executes `ERET` back to EL0 with zero user register corruption.

## 2. Implementation & Test Architecture
The test suite `tests/unit/stage193_11_stack_management_tests.cpp` exercises:
1. `test_el0_el1_stack_switching_and_preservation`:
   - Configures `SP_EL0 = 0x7FFFF000` and `SP_EL1 = 0x50000`.
   - EL0 executes `SVC #0x10`: enters EL1h with active SP = `0x50000`, while `SP_EL0` remains intact at `0x7FFFF000`.
   - EL1 executes `ERET`: returns to EL0 with SP restored to `0x7FFFF000` and `X0` intact.
2. `test_spsel_selection_at_el1`:
   - Toggles `sp_sel` at EL1 between false (EL1t) and true (EL1h), verifying `get_stack_pointer()` returns `SP_EL0` and `SP_EL1` respectively.
3. `test_sp_alignment_fault_at_el0_and_el1`:
   - Enables `SCTLR_EL1.SA0` and `SA`.
   - Unaligned `SP_EL0` (0x20005) on `LDR X0, [SP]` traps to `VBAR_EL1 + 0x400` with EC `0x26`.
   - Unaligned `SP_EL1` (0x30008) on `LDR X0, [SP]` traps to `VBAR_EL1 + 0x200` with EC `0x26`.
4. `test_nested_exception_context_preservation`:
   - Round-trip EL0 -> EL1 (SVC) -> EL1 nested fault (undefined opcode) -> nested return to outer handler -> outer return to EL0.
   - Proves complete context preservation across nested privilege levels.

## 3. Evidence & Verification Results
- `tests/unit/stage193_11_stack_management_tests.cpp` (4/4 tests passed in both Debug and Release).
- CTest target: `stage193_11_stack_management_tests`.
- Classification: `GUEST_EL0` / `GUEST_EL1` / `GUEST_EXCEPTION` / `HOST_MODEL`.
