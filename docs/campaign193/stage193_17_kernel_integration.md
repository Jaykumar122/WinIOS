# Stage 193.17 — Freestanding Kernel Integration Tests

## 1. Objectives & Scope
Stage 193.17 integrates the privilege, exception, MMU and interrupt mechanisms developed across Campaign 193 into end-to-end guest execution workflows:
1. Guest System Call Round-Trip & Callee-Saved Register Preservation:
   - Guest EL0 user code issues `SVC #0` passing arguments in `X0..X7`.
   - Hardware vectors to `VBAR_EL1 + 0x400` (Lower EL AArch64 Sync).
   - Privilege escalates to EL1, saving `SPSR_EL1` (EL0t) and `ELR_EL1` (user return address).
   - Kernel executes handler instructions, mutates return values, and executes `ERET`.
   - Execution resumes at EL0, verifying that all callee-saved registers (`X19..X28`, `FP`, `LR`), stack pointers, and `NZCV` condition codes remain intact.
2. Demand-Paging Resolution & TLB Invalidation:
   - Stage-1 16KB-granule MMU is active (`TCR_EL1.TG0 = 2`, `T0SZ = 28`, `TTBR0_EL1` page table root).
   - EL0 user instruction executes a load from unmapped virtual address (`0x8000`).
   - Hardware raises translation fault (DFSC `0x06`), vectoring to `VBAR_EL1 + 0x400` with `FAR_EL1 = 0x8000` and `ELR_EL1` set to the faulting instruction PC.
   - Kernel demand-paging service dynamically allocates physical page frame (`0x20000`), writes valid Level 3 page table entry, issues `TLBI VAAE1` invalidation for `0x8000`, and executes `ERET`.
   - EL0 re-executes the load instruction successfully, loading expected data without application fault.
3. Timer Interrupt Servicing & Task Context Switching:
   - ARM64 Generic Virtual Timer configured with expiration threshold.
   - Clock advancement past compare value asserts GICv3 PPI 27.
   - Hardware vectors to `VBAR_EL1 + 0x480` (Lower EL AArch64 IRQ), setting `DAIF = 0xF`.
   - Kernel interrupt handler acknowledges PPI 27 via `ICC_IAR1_EL1`, masks timer via `CNTV_CTL_EL0`, saves Task A state (`ELR_EL1 = 0x1000`), activates Task B state (`ELR_EL1 = 0x2000`), executes `ICC_EOIR1_EL1`, and issues `ERET`.
   - Execution seamlessly branches into Task B at EL0.
4. Exception Nesting Under Kernel Control:
   - EL0 triggers synchronous exception (`SVC #100`) -> enters EL1 `+0x400`.
   - Kernel handler preserves outer return state (`ELR_EL1`, `SPSR_EL1`) onto EL1 stack, and intentionally triggers a nested exception from EL1 (`SVC #200`).
   - Vector dispatch correctly selects Current EL with SPx vector (`VBAR_EL1 + 0x200`).
   - Inner handler executes, returns via `ERET` to outer kernel handler.
   - Outer handler restores saved frame and returns via `ERET` to EL0, resuming user execution with 100% state integrity.

## 2. Implementation & Test Architecture
The test suite `tests/unit/stage193_17_kernel_integration_tests.cpp` exercises:
- `test_integration_syscall_invocation_and_register_preservation`: 4/4 assertions pass.
- `test_integration_demand_paging_and_tlb_invalidation`: 4/4 assertions pass.
- `test_integration_timer_interrupt_servicing_and_task_switch`: 4/4 assertions pass.
- `test_integration_nested_exception_under_kernel_control`: 4/4 assertions pass.

## 3. Evidence & Verification Results
- `tests/unit/stage193_17_kernel_integration_tests.cpp` (4/4 tests passed in both Debug and Release).
- CTest target: `stage193_17_kernel_integration_tests`.
- Classification: `SYNTHETIC_KERNEL` / `GUEST_EL1` / `GUEST_EL0` / `GUEST_EXCEPTION` / `GUEST_INTERRUPT`.
