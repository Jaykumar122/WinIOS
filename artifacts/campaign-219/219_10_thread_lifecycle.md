# WinIOS Campaign 219 — Stage 219.10: Thread Lifecycle, Termination & Join

## 1. Thread Creation (`SYS_winios_thread_create`, 524)

- Takes arguments:
  - `arg0`: Initial entry point VA (`pc`)
  - `arg1`: Thread argument passed in register `X0`
  - `arg2`: Virtual address base of dedicated user stack
  - `arg3`: Stack size in bytes (defaults to 64 KiB if 0)
- Instantiates a new `KernelThread` via `KernelThreadManager`.
- Initial stack pointer (`SP_EL0`) is aligned to the top of the allocated stack (`(stack_base + stack_size) & ~0xFULL`).
- `SPSR_EL1` is initialized to user mode (`0b0000 = EL0t`).
- The thread is attached to `proc.threads()` and enqueued in `MultiProcessScheduler`.

## 2. Thread Termination (`SYS_winios_thread_exit`, 525)

- Takes `arg0 = exit_code`.
- Sets active thread state to `ThreadState::Terminated`.
- Records return code in `tcb().exit_code`.
- Yields CPU immediately to the next ready thread in the scheduler queue.

## 3. Thread Join & Isolation (`SYS_winios_thread_join`, 526)

- Takes:
  - `arg0`: Target thread ID (`tid`)
  - `arg1`: User pointer where exit status `int32_t` should be written
- **Security Check**: The dispatcher verifies that `th->owner_pid() == proc.pid()`. A process is strictly forbidden from joining threads belonging to another process; violation returns `ESRCH` (3).
- If the thread is still active, returns `EBUSY` (16).
- If terminated, writes exit status to user memory and returns 0 (`KERN_SUCCESS`).

## 4. Test Verification

Verified by `stage219_10_synchronization_tests.exe`:
- Spawning worker thread with argument `0x42`.
- Attempted join while worker active returns `EBUSY`.
- Worker execution and exit with status 77.
- Successful join delivering status 77.
- Foreign process join attempt rejected with `ESRCH` (3).
