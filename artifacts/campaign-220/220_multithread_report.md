# WinIOS Campaign 220 — Stage 220.21 Report
## Genuine Multi-Thread Guest Workload Execution & Mutex Synchronization

**Stage:** 220.21  
**Status:** COMPLETE & VERIFIED  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), ARM64, 16 KB Granule  
**Host:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Cross Compiler:** Zig 0.17.0 (`zig cc -target aarch64-macos -nostdlib -Wl,-e,_worker`)

---

### 1. Executive Summary

Stage 220.21 validates genuine multi-threaded ARM64 guest execution, round-robin intra-process thread scheduling, mutex synchronization, and thread lifecycle semantics within a single guest address space (PID 200, ASID 3).

A main thread (`th_main`) spawned two worker threads (`th_w1`, `th_w2`) executing genuine ARM64 Mach-O code compiled via Zig 0.17.0. The worker threads competed for a shared 4-byte aligned mutex protecting a shared memory counter. The threads yielded quanta back and forth through `MultiProcessScheduler`, validating identical-address-space context switching (`same_process_context_switches >= 2`) with unchanged TTBR0_EL1. Each worker completed 25 critical section increments, verifying mutual exclusion with zero lost updates (final counter value: 50). Both threads exited with status 42 via `SYS_winios_thread_exit` (521) and were successfully joined and reaped by the main thread via `SYS_winios_thread_join` (522).

---

### 2. Multi-Thread Architectural Validation

#### 2.1 Thread Creation & Context Alignment
- **Worker Binary:** `campaign220_worker.bin` cross-compiled with Zig 0.17.0.
- **Stacks:** Allocated 64 KB user stacks per thread, 16-byte aligned at `stack_top`:
  - `th_main`: Base `0x7FFE0000`, Top `0x7FFF0000`
  - `th_w1`: Base `0x7FFC0000`, Top `0x7FFD0000`
  - `th_w2`: Base `0x7FFA0000`, Top `0x7FFB0000`
- **Creation Trap:** `kSysWiniosThreadCreate` (520) verified entry address 4-byte alignment, stack base/size validation, and distinct TIDs.

#### 2.2 Mutual Exclusion & Round-Robin Scheduling
- **Shared Memory Region:** Page mapped at `0x20000000` with RW permissions containing `mutex` (offset 0), `counter` (offset 4), `iterations` (offset 8), and exit statuses (offsets 16, 24).
- **Locking Semantics:** `kSysWiniosMutexLock` (524) and `kSysWiniosMutexUnlock` (525) enforced ownership by calling thread TID and rejected unauthorized operations.
- **Round-Robin Scheduling:** Round-robin yields between `th_main`, `th_w1`, and `th_w2` verified thread queue rotation and register preservation.

#### 2.3 Join and Exit Semantics
- Both workers terminated via `kSysWiniosThreadExit` with code 42.
- Main thread issued `kSysWiniosThreadJoin` with target TIDs and out-status pointers:
  - Worker 1 status written to shared memory: 42
  - Worker 2 status written to shared memory: 42
  - Zero memory leaks, zombie threads cleanly reaped.

---

### 3. Verification Metrics

- **Test Suite:** `tests/unit/stage220_21_multi_thread_tests.cpp` (CTest #784).
- **Verified Instruction Retirement:** 50 genuine ARM64 instructions retired on the CPU core and recorded in `ExecutionBoundaryEnforcer`.
- **Intra-Process Switches:** `same_process_context_switches >= 2` verified.
- **Synchronization Integrity:** Exactly 50 increments recorded (25 from Worker 1 + 25 from Worker 2).
