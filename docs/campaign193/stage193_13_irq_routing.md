# Stage 193.13 — IRQ/FIQ Exception Routing

## 1. Objectives & Scope
Stage 193.13 connects the Generic Interrupt Controller (GICv3) delivery pipeline to the centralized ARM64 exception architecture:
- IRQ exception entry and vector dispatch:
  - From guest EL0 (AArch64): vectors to `VBAR_EL1 + 0x480` (Lower EL AArch64 IRQ).
  - From guest EL1 with SP_EL1 (EL1h): vectors to `VBAR_EL1 + 0x280` (Current EL SPx IRQ).
- FIQ exception entry:
  - From guest EL0: vectors to `VBAR_EL1 + 0x500` (Lower EL AArch64 FIQ).
  - Acknowledged via `ICC_IAR0_EL1` (`0xD538C800`) and completed via `ICC_EOIR0_EL1` (`0xD518C820`).
- PSTATE interrupt masking:
  - While `DAIF.I` (bit 1) is 1, asynchronous IRQ exceptions are held pending without interrupting instruction execution.
  - When `DAIF.I` is cleared to 0, the pending IRQ is immediately taken before instruction fetch.
  - On exception entry, `DAIF` is automatically masked (`0xF`), preventing reentrant interrupts inside the handler.
- Saved return state:
  - `ELR_EL1` holds the exact instruction address where execution was preempted.
  - `SPSR_EL1` saves the prior `NZCV`, `DAIF`, execution level, and `SPSel`.
- Complete round-trip guest handler execution:
  - Preemption -> vector dispatch -> handler executes `MRS ICC_IAR1_EL1` -> deasserts source -> handler executes `MSR ICC_EOIR1_EL1` -> handler executes `ERET` -> CPU returns to EL0 and resumes user execution with zero duplicate interrupt delivery.

## 2. Implementation & Test Architecture
The test suite `tests/unit/stage193_13_irq_routing_tests.cpp` exercises:
1. `test_el0_irq_exception_routing_and_return`:
   - Guest EL0 executes user loop (`ADD X1, X1, #1` at `0x1000`).
   - GIC asserts Group 1 SPI 36.
   - CPU preempts instruction, jumps to `VBAR_EL1 + 0x480`, sets `ELR_EL1 = 0x1000`, `DAIF = 0xF`.
   - Guest handler acknowledges INTID 36, deasserts line, completes with EOIR1, and executes `ERET`.
   - EL0 resumes at `0x1000`, completes `ADD` instruction, with `X1` incremented and zero duplicate delivery.
2. `test_el1_irq_exception_routing_and_return`:
   - Guest EL1 executes at `0x2000` with `DAIF = 0`.
   - GIC asserts SPI 37.
   - Vector jump to Current EL SPx IRQ vector `VBAR_EL1 + 0x280`.
   - Handler acknowledges INTID 37, performs EOI, and executes `ERET`, resuming EL1 at `0x2000`.
3. `test_daif_i_masking_prevents_delivery`:
   - While `DAIF.I` is set, user instructions execute without interruption despite an active pending IRQ in GIC.
   - Once `DAIF.I` is cleared, the interrupt is immediately delivered on the next step.
4. `test_fiq_exception_routing`:
   - GIC asserts Group 0 SPI 39.
   - CPU vectors to Lower EL FIQ vector `VBAR_EL1 + 0x500`.
   - Handler acknowledges via `ICC_IAR0_EL1`, performs `ICC_EOIR0_EL1`, and returns via `ERET`.

## 3. Evidence & Verification Results
- `tests/unit/stage193_13_irq_routing_tests.cpp` (4/4 tests passed in both Debug and Release).
- CTest target: `stage193_13_irq_routing_tests`.
- Classification: `GUEST_INTERRUPT` / `GUEST_EXCEPTION` / `GUEST_EL0` / `GUEST_EL1`.
