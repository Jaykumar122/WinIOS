# WinIOS Campaign 219 — Stage 219.7: Deterministic Multi-Process Scheduler Design

## 1. Scheduler Architecture

`MultiProcessScheduler` orchestrates multi-process and multi-thread execution across the virtual ARM64 CPU. It manages a deterministic time-slice quantum and ready queue:

- **Time-Slice Quantum**: Fixed at 4 scheduling ticks (`kDefaultTimeSliceQuantum = 4`).
- **Dispatch Queue**: FIFO round-robin ready queue (`std::deque<KernelThread*>`).
- **Process Address Space Mapping**: `uas_map_` maps `pid_t -> memory::UserAddressSpace*`.

## 2. Scheduling Policies & Events

### 1. Quantum Tick & Preemption (`tick(cpu)`)
- On each virtual timer tick, `quantum_remaining_` is decremented.
- When `quantum_remaining_ == 0`:
  - Current thread state transitions from `Running` to `Ready`.
  - Current thread is pushed to the back of `ready_queue_`.
  - Next ready thread is popped from the front.
  - Context switch is executed (`perform_context_switch`).
  - `stats_.total_preemptions` is incremented.

### 2. Voluntary Yield (`yield(cpu)`)
- Invoked via POSIX `SYS_sched_yield` (SYS 329).
- Current thread voluntarily gives up remainder of its quantum.
- Thread transitions to `Ready` and is enqueued.
- Immediate switch to next ready thread.
- `stats_.total_yields` is incremented.

### 3. Starvation Prevention & Quantum Recharge
- If the ready queue is empty when quantum expires, the current thread continues running and its quantum is recharged back to 4 ticks.
