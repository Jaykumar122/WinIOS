# Stage 193.15 — Exception Nesting and SMP Delivery

## 1. Objectives & Scope
Stage 193.15 validates multi-core exception delivery, per-core isolation, nested exception handling, and inter-processor interrupt delivery:
- Per-Core Exception State Isolation:
  - Each CPU core maintains independent exception vectors (`VBAR_EL1`), link registers (`ELR_EL1`), syndrome registers (`ESR_EL1`), and saved processor status (`SPSR_EL1`).
  - Exceptions taken on Core 0 leave Core 1's architectural exception state completely unperturbed.
- Per-Core Stack Pointer Isolation:
  - Separate `SP_EL1` allocations per core (`0x80000` for Core 0, `0x90000` for Core 1).
  - Stack pushes and frame stores are isolated in memory without cross-core corruption.
- Bounded Nested Exception Handling (EL0 -> EL1 outer -> EL1 inner -> EL1 outer -> EL0):
  - Software at EL1 preserves outer `ELR_EL1` and `SPSR_EL1` onto its stack before unmasking or triggering nested exceptions.
  - Nested exception from EL1 takes the Current EL with SPx vector (`VBAR_EL1 + 0x200` or `+ 0x280`).
  - Inner exception handler executes, returns via `ERET` to outer EL1 handler.
  - Outer handler restores saved state and executes `ERET` to return cleanly to EL0.
- Inter-Core Interrupt Delivery (GICv3 SGI):
  - Software Generated Interrupts (SGI 0..15) routed via GICv3 Redistributors.
  - Core 0 sends SGI 5 targeted strictly to Core 1 via `ICC_SGI1R_EL1` (`send_sgi`).
  - Only Core 1 receives the pending interrupt and takes the Lower EL IRQ vector (`VBAR_EL1 + 0x480`).
  - Core 1 acknowledges SGI 5 via `ICC_IAR1_EL1`, completes via `ICC_EOIR1_EL1`, and returns cleanly to EL0.
  - Core 0 experiences zero false or spurious interrupt preemption.

## 2. Implementation & Test Architecture
The test suite `tests/unit/stage193_15_smp_exception_tests.cpp` exercises:
1. `test_per_core_exception_state_isolation`:
   - Dual-core cluster (`SMPCluster(2)`).
   - Core 0 takes `SVC #10` -> vectors to `vbar0 + 0x400`, sets `ESR_EL1` immediate 10.
   - Core 1 verified unmutated at EL0.
   - Core 1 takes `SVC #20` -> vectors to `vbar1 + 0x400`, sets `ESR_EL1` immediate 20.
   - Core 0 state verified intact.
2. `test_per_core_smp_exception_stack_isolation`:
   - Confirms independent stack pointers and canary preservation across cores in EL1.
3. `test_nested_exception_handling_el1_to_el1`:
   - EL0 `SVC #1` vectors to `+0x400` -> handler triggers nested `SVC #2` -> vectors to `+0x200` (Current EL SPx) -> nested handler executes -> `ERET` to outer handler -> outer handler restores state and `ERET` to EL0 -> user instruction executed.
4. `test_inter_core_sgi_interrupt_delivery`:
   - Core 0 writes SGI 5 targeting Core 1. Core 1 preempted at EL0, handles INTID 5 at `+0x480`, performs EOI, and resumes user code.

## 3. Evidence & Verification Results
- `tests/unit/stage193_15_smp_exception_tests.cpp` (4/4 tests passed in both Debug and Release).
- CTest target: `stage193_15_smp_exception_tests`.
- Classification: `GUEST_EXCEPTION` / `GUEST_INTERRUPT` / `GUEST_EL0` / `GUEST_EL1`.
