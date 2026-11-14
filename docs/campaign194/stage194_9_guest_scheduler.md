# WINIOS CAMPAIGN 194: STAGE 194.9 REPORT
## Guest Priority Scheduler & Run Queues

### 1. Executive Summary
Stage 194.9 establishes the multi-level priority scheduler and run-queue architecture for the WinIOS ARM64 EL1 kernel runtime.
Key deliverables:
- **32-Level Priority Queues:** 32 distinct run queues (`0..31`) indexed by thread priority, with an active queue bitmap (`ready_bitmap_`) supporting $O(1)$ priority selection.
- **Round-Robin Scheduling:** Fair FIFO scheduling among threads sharing identical priority levels.
- **System Idle Thread:** Auto-instantiated priority-0 idle thread executing an authentic ARM64 `WFI` idle loop in guest memory when all user/kernel queues are quiescent.
- **Preemptive Quantum Enforcement:** Per-thread quantum decremented on clock ticks, triggering preemptive context switching to the next ready thread when depleted.
- **Strict Priority Preemption:** Dynamic preemption when a higher-priority thread becomes ready while a lower-priority thread is executing.
- **Voluntary Yielding & Blocking:** Cooperative `yield` operations and fine-grained `block_current(WaitReason)` / `unblock_thread` lifecycle transitions.

---

### 2. Architecture & Design

#### 2.1 Multi-Level Run Queues & Ready Bitmap
- **Queues:** `std::array<std::deque<KernelThread*>, 32> run_queues_`.
- **Bitmap:** `uint32_t ready_bitmap_` where bit $k$ indicates presence of ready threads in queue $k$.
- **Selection:** `pick_next_thread()` scans from bit 31 down to 0, returning the head of the highest non-empty queue in $O(1)$ time, or falling back to `idle_thread_` if the bitmap is zero.

#### 2.2 Guest Idle Loop Routine
`PriorityScheduler::emit_guest_idle_routine` generates authentic ARM64 guest instructions:
```arm64
.Lidle_loop:
    wfi        // 0xD503207Fu (Wait For Interrupt)
    b . - 4    // 0x17FFFFFFu (Branch back to WFI)
```
Written directly into guest RAM at `idle_code_addr`, allowing the virtual CPU to safely idle in guest code until awakened by interrupts.

#### 2.3 Preemption Logic
On each `tick()`:
1. If running thread's time slice expires (`time_slice == 0`), quantum resets and scheduler preempts to the next ready thread.
2. If any higher-priority bit in `ready_bitmap_` is set, scheduler immediately preempts the current thread and context-switches to the higher-priority thread.

---

### 3. Verification & Results

Unit test suite `tests/unit/stage194_9_guest_scheduler_tests.cpp` executes 7 test cases:
1. `test_scheduler_initialization_and_idle_thread`: Validates idle thread creation, priority 0, and guest RAM WFI instructions.
2. `test_priority_queueing_and_selection`: Validates strict priority ordering (30 -> 20 -> 10 -> idle) from out-of-order enqueueing.
3. `test_round_robin_among_equal_priorities`: Validates fair FIFO round-robin progression across threads sharing priority 16.
4. `test_quantum_expiration_and_preemption`: Verifies quantum decrement, expiration preemption, and thread re-queueing.
5. `test_higher_priority_preemption`: Verifies immediate preemption when a priority 31 thread arrives while priority 16 is executing.
6. `test_voluntary_yield_and_blocking`: Verifies cooperative `yield`, `block_current(Mutex)`, and `unblock_thread`.
7. `test_guest_arm64_idle_execution`: Steps virtual CPU through guest idle loop (`WFI` and loop branch).

Both Release and Debug configurations build cleanly under `/W4 /WX /permissive-` and pass all 7 tests with zero errors.

---

### 4. Classification
- Source implementations: `GUEST_SCHEDULER` / `SYNTHETIC_KERNEL`
- Simulation / host harness: `HOST_MODEL`
