# Campaign 237 — Stage 237.6: Scheduler & Runtime Failure Repairs

## 1. Executive Summary

Stage 237.6 successfully resolved all failures identified in Cluster 2 (Scheduler & Runtime Subsystem), comprising:
- `stage212_17_init_coordinator_tests` (Test #665)
- `stage212_18_multi_process_scheduler_tests` (Test #666)
- `stage219_8_scheduler_execution_tests` (Test #756)
- `stage219_10_synchronization_tests` (Test #757)
- Bonus fix: `stage220_10_thread_lifecycle_tests` (Test #775)

All 5 test suites now pass 100% in `RelWithDebInfo` and `Debug` configurations.

---

## 2. Root Cause Analysis & Technical Repairs

### 2.1 ARM64 TTBR0 ASID Masking (#666 & #756)
- **Root Cause**: `MultiProcessScheduler` packs the 16-bit ASID into bits [63:48] of the ARM64 `TTBR0_EL1` register using `root_table_pa_with_asid()`. Unit tests in `stage212_18` and `stage219_8` asserted exact equality between `cpu.get_mmu().get_ttbr0_el1()` and the raw physical address `root_pa`. Because the register contained non-zero ASID bits, the assertion failed.
- **Correction**: Updated `tests/unit/stage212_18_multi_process_scheduler_tests.cpp` (lines 74, 80) and `tests/unit/stage219_8_scheduler_execution_tests.cpp` (lines 150, 164, 173) to mask `TTBR0_EL1` with `0x0000FFFFFFFFFFFFULL` (the standard 48-bit physical address mask), preserving genuine ARM64 hardware behavior.

### 2.2 POSIX Mutex Trylock Return Code (#757)
- **Root Cause**: When `kSysWiniosMutexLock` was called with `arg1 == 1` (`is_trylock`), `UserSyscallDispatcher` previously returned `11 /* EDEADLK */` unconditionally upon contention. POSIX and WinIOS specifications require non-blocking `pthread_mutex_trylock` to return `16 /* EBUSY */` when the mutex is already locked.
- **Correction**: In `src/kernel/user_syscall_dispatcher.cpp` lines 1262–1270, distinguished trylock calls:
  ```cpp
  const bool is_trylock = (arg1 == 1);
  if (is_trylock) {
      return record_error(16 /* EBUSY */);
  }
  return record_error(11 /* EDEADLK */);
  ```

### 2.3 Thread Join Caller Deadlock & Exit State Observability (#757)
- **Root Cause**:
  1. In `UserSyscallDispatcher::handle_syscall` (`case kSysWiniosThreadJoin`), `caller_tid` was hardcoded to 1 when no active thread was running on the CPU (`multi_sched_->current_thread() == nullptr` and `tpidr_el0 == 0`). Since `th_mgr.create_thread` assigned TID 1 to the first worker thread, `target_tid == caller_tid` was evaluated as true, erroneously reporting self-deadlock (`EDEADLK`).
  2. `MultiProcessScheduler::terminate_thread` immediately set `current_thread_ = nullptr`, causing `scheduler.current_thread()->state()` to dereference a null pointer during post-exit state assertions.
- **Correction**:
  1. In `src/kernel/user_syscall_dispatcher.cpp`, updated caller determination so only active, non-terminated threads trigger deadlock detection:
     ```cpp
     std::uint64_t caller_tid = 0;
     if (multi_sched_ && multi_sched_->current_thread() &&
         multi_sched_->current_thread()->state() != ThreadState::Terminated) {
         caller_tid = multi_sched_->current_thread()->tid();
     } else if (cpu.get_tpidr_el0() != 0) {
         caller_tid = cpu.get_tpidr_el0();
     }
     if (caller_tid != 0 && target_tid == caller_tid) {
         return record_error(11 /* EDEADLK */);
     }
     ```
  2. In `src/kernel/multi_process_scheduler.cpp`, preserved `current_thread_` across `terminate_thread` so that callers can inspect the terminated thread's final exit code and state before the subsequent context switch.

### 2.4 AutoDetect ABI Error Code Propagation (#775)
- **Root Cause**: In `UserSyscallDispatcher::handle_syscall`, `record_success` and `record_error` checked `if (abi_mode_ == SyscallAbi::DarwinStrict)`. When configured with `SyscallAbi::AutoDetect`, the dispatcher correctly detected Darwin syscalls via `X16 != 0` and set local boolean `is_darwin_strict = true`, but `record_error` fell through to the Fast ABI branch, returning negative error codes instead of Darwin positive errno values in `X0`.
- **Correction**: Changed condition to `if (is_darwin_strict)` in both `record_success` and `record_error`, restoring correct positive errno and PSTATE.C semantics across all AutoDetect calls.

### 2.5 Init Process PID 1 Initialization (#665)
- **Root Cause**: `BsdProcessManager::initialize()` initializes the kernel process at PID 0. The `InitCoordinator` assumes the existence of the Init supervisor process at PID 1 to handle supervision, reaping, and reparented orphans. In unit tests where PID 1 was not explicitly spawned, services spawned by `InitCoordinator` were incorrectly assigned PID 1, and `wait_process(1, ...)` failed with `-ECHILD`.
- **Correction**: In `src/kernel/init_coordinator.cpp`, the `InitCoordinator` constructor now ensures PID 1 exists:
  ```cpp
  if (!proc_mgr_.get_process(1)) {
      proc_mgr_.create_process("init", 0);
  }
  ```

---

## 3. Verification Matrix

| Test Suite | Test # | Previous Result | Current Result | Execution Time |
|---|---|---|---|---|
| `stage212_17_init_coordinator_tests` | 665 | Failed (8 sub-checks failed) | **PASSED** (100%) | 0.03s |
| `stage212_18_multi_process_scheduler_tests` | 666 | Failed (TTBR0 comparison) | **PASSED** (100%) | 0.03s |
| `stage219_8_scheduler_execution_tests` | 756 | Failed (TTBR0 comparison) | **PASSED** (100%) | 0.02s |
| `stage219_10_synchronization_tests` | 757 | Failed (trylock & join errno) | **PASSED** (100%) | 0.01s |
| `stage220_10_thread_lifecycle_tests` | 775 | Failed (AutoDetect ABI negative errno) | **PASSED** (100%) | 0.03s |

Total Cluster 2 tests passed: **5 / 5 (100%)**. Zero regressions.
