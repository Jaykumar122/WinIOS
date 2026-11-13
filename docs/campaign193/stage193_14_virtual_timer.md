# Stage 193.14 — Virtual Timer Interrupts

## 1. Objectives & Scope
Stage 193.14 verifies the ARM64 Generic Timer virtual timer architecture integrated with the CPU and GICv3 interrupt subsystem:
- System Counter & Frequency:
  - Default 24 MHz monotonic system physical counter (`CNTFRQ_EL0`).
  - Monotonic clock progression via `advance_physical_counter`.
- Virtual Counter Evaluation:
  - `CNTVCT_EL0 = CNTPCT - CNTVOFF_EL2`.
  - Offset calculation per virtual core context.
- Virtual Timer Control and Expiration:
  - `CNTV_CTL_EL0` register semantics: bit 0 (`ENABLE`), bit 1 (`IMASK`), bit 2 (`ISTATUS`).
  - Compare value register `CNTV_CVAL_EL0`.
  - Assert condition: `ENABLE && (CNTVCT >= CNTV_CVAL)`.
  - IRQ assertion condition: `ENABLE && ISTATUS && !IMASK`.
  - Masking condition: when `IMASK` is set, `irq_asserted()` is false, but `ISTATUS` remains set if `CNTVCT >= CNTV_CVAL`.
- Interrupt Delivery to Guest EL1 Handler:
  - Expiration of `CNTV` asserts GICv3 private peripheral interrupt PPI 27.
  - Preempts guest EL0 instruction execution, saving return address in `ELR_EL1` and PSTATE in `SPSR_EL1`.
  - Vectors to `VBAR_EL1 + 0x480` (Lower EL AArch64 IRQ vector) with `DAIF` masked.
  - Guest EL1 handler executes:
    - `MRS X0, ICC_IAR1_EL1` (reading INTID 27).
    - `MOV X2, #2` followed by `MSR CNTV_CTL_EL0, X2` to mask the virtual timer.
    - `MSR ICC_EOIR1_EL1, X0` to complete the interrupt in GIC.
    - `ERET` to return to EL0 and resume user execution cleanly.
- Context Switch Integrity:
  - Dynamic switching of `CNTVOFF_EL2` preserves and isolates virtual timer timelines across guest process contexts.

## 2. Implementation & Test Architecture
The test suite `tests/unit/stage193_14_virtual_timer_tests.cpp` exercises:
1. `test_timer_counter_and_compare_evaluation`:
   - Validates frequency query, monotonic physical advance, and `CNTVOFF_EL2` offset arithmetic.
2. `test_timer_enable_mask_and_expiration`:
   - Validates expiration state changes, `ISTATUS` retention, and `IMASK` deassertion of the IRQ line.
3. `test_timer_interrupt_delivery_to_guest_el1_handler`:
   - Full guest execution flow: user code execution at EL0 (`ADD X1, X1, #1` at `0x1000`) -> clock advance -> PPI 27 interrupt -> vectoring to `VBAR_EL1 + 0x480` -> EL1 handler reads `ICC_IAR1_EL1` (INTID 27), masks timer via `MSR CNTV_CTL_EL0`, executes `MSR ICC_EOIR1_EL1`, returns via `ERET` -> EL0 resumes and finishes `ADD` instruction without repeat triggering.
4. `test_timer_behavior_across_context_switch`:
   - Validates multi-process virtual counter time shifting via `CNTVOFF_EL2`.

## 3. Evidence & Verification Results
- `tests/unit/stage193_14_virtual_timer_tests.cpp` (4/4 tests passed in both Debug and Release).
- CTest target: `stage193_14_virtual_timer_tests`.
- Classification: `GUEST_INTERRUPT` / `GUEST_EXCEPTION` / `GUEST_EL0` / `GUEST_EL1`.
