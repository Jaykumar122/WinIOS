# Campaign 194 Stage 194.17: Kernel Diagnostics & Failure Recovery

## Objective
Provide diagnostic logging, architectural crash dumping, and fault recovery for the synthetic kernel runtime:
1. `KernelPanic`:
   - Structured panic record (`PanicReason`, message string, faulting PC, EL, ESR, FAR, GPR dump, backtrace frames).
   - Formatted human-readable panic report string with stack trace and register values.
2. Stack unwinding & backtrace:
   - Walk frame pointer chain (`X29` / FP, `X30` / LR) through guest memory.
   - Strict boundary checking: abort unwinding if FP is null, unaligned, or points outside stack boundaries.
   - Circular reference and backward stack detection to prevent infinite loops.
3. Subsystem diagnostic log:
   - Circular log buffer in kernel memory for structured kernel log entries (`LogLevel`: Debug, Info, Warn, Error, Panic).
   - Timestamped entries with subsystem tags (BOOT, MEM, SCHED, IPC, DRV, CPU).

## Implementation Details
- Created `src/kernel/panic.h` and `src/kernel/panic.cpp`:
  - `LogLevel` and `PanicReason` enumerations.
  - `StackFrame` and `PanicInfo` structures.
  - `KernelDiagnostics` circular log buffer management.
  - `unwind_stack()` verifying 8-byte alignment, stack memory boundary constraints, and strictly monotonic frame progression.
  - `format_panic_report()` generating diagnostic dumps.
- Authored test suite `tests/unit/stage194_17_kernel_diagnostics_tests.cpp`.

## Test Results
- `stage194_17_kernel_diagnostics_tests`:
  - `test_panic_info_and_formatting`: PASSED (formatted diagnostic report containing syndrome, registers, backtrace).
  - `test_diagnostic_logging_circular_buffer`: PASSED (circular ring buffer FIFO replacement).
  - `test_stack_unwinding_valid_frames`: PASSED (3-level frame pointer unwinding across guest memory).
  - `test_stack_unwinding_corrupted_frames`: PASSED (misaligned FP, out-of-bounds FP, and circular loops handled safely).
  - `test_trigger_panic_and_state`: PASSED (panic state latching and logging).
  - Total: 29 passed, 0 failed.

## Evidence Classification
- Kernel Diagnostics & Unwinding: `SYNTHETIC_KERNEL`
