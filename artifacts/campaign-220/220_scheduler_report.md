# WinIOS Campaign 220 — Stage 220.7 Scheduler State Machine & Fairness Report

## 1. Executive Summary

Stage 220.7 audited and hardened the `MultiProcessScheduler` state machine, introducing formal invariant validation, configurable time-slice quantums, explicit blocking and unblocking APIs, and deterministic round-robin queue management.
Key achievements:
1. **Thread Lifecycle State Machine:**
   - Formalized transitions across `Ready`, `Running`, `Blocked` (with typed `WaitReason`), and `Terminated`.
   - Added `block_thread(th, reason)`: safely deschedules the thread, updates its wait reason (e.g. `WaitReason::Mutex`, `WaitReason::IpcMessage`), and removes it from the ready queue.
   - Added `unblock_thread(th)`: transitions blocked thread back to `Ready` and enqueues it.
   - Added `terminate_thread(th, exit_code)`: transitions thread to `Terminated` and removes it from active scheduling.
2. **Deterministic Round-Robin Fairness:**
   - Verified that multiple concurrent threads (Threads A, B, C) cycle in strict order `A -> B -> C -> A -> B -> C` without thread starvation or priority inversion.
3. **Configurable Time-Slice Quantum:**
   - Introduced `set_time_slice_quantum(quantum)` allowing dynamic configuration of quantum length (e.g. 2 ticks, 4 ticks) while retaining deterministic tick decrements.
   - Verified timer preemption on quantum expiration: when `quantum_remaining_ == 0`, scheduler preempts current thread and context-switches to the next ready thread.
4. **Scheduler Invariants:**
   - Added `check_invariants()`: asserts that `current_thread_` (if non-null) is strictly in `ThreadState::Running`, and all threads in `ready_queue_` are strictly in `ThreadState::Ready`.

---

## 2. State Machine Transitions Matrix

| Initial State | Event / Trigger | Target State | Invariant Maintained |
| :--- | :--- | :--- | :---: |
| `Ready` | `schedule_next()` / `yield()` | `Running` | Active thread registered on CPU |
| `Running` | `yield()` / `tick()` (quantum expired) | `Ready` | Enqueued at tail of `ready_queue_` |
| `Running` | `block_thread(reason)` | `Blocked` | Removed from queue, CPU unassigned |
| `Blocked` | `unblock_thread()` | `Ready` | Appended to `ready_queue_` |
| `Running` / `Ready` | `terminate_thread(code)` | `Terminated` | Removed from queue, exit code saved |
| `Terminated` | `reap_zombies()` | Cleaned up | TCB and stack reclaimed |
