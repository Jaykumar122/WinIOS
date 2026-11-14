# Stage 195.0 — Baseline and Evidence Audit

## 1. Executive Summary & Baseline Identification

This document establishes the official starting baseline and architectural audit for **Campaign 195: Kernel Correctness, Preemption Integrity, Complete CPU Context Preservation & SMP Stress Validation**.

- **Repository:** `D:\WinISO26\WinIOS\`
- **Branch:** `campaign-191r`
- **Baseline Git Commit:** `9e070ce7e1f5fb88b49253e3ddcec423efce62a5` (Campaign 194 closeout)
- **Host OS:** Windows 11 x64
- **Toolchain:** Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)
- **Secondary Toolchain:** Zig 0.17.0 (Clang 22.1.8 + LLVM Mach-O Linker)
- **Target Research Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`, ARM64 / ARM64e)
- **Baseline CTest Targets:** 384 total targets
  - **Release:** 383 passed, 1 skipped (`inferno_integration_tests`), 0 failed (100% pass rate)
  - **Debug:** 383 passed, 1 skipped (`inferno_integration_tests`), 0 failed (100% pass rate)

---

## 2. Non-Negotiable Safety and Protected Files Verification

All mandatory safety invariants were audited and verified before any changes were introduced:

| Protected Resource | Path / Hash | Verified State | Audit Status |
| :--- | :--- | :--- | :--- |
| `WinIOS_README.md` | `61f4d078d5071b5ab16093eb5418ed15afb7ed41cd3c84f080ce44e196ea7fef` | Verified identical | **UNTOUCHED** |
| `docs/progress.md` | `447e7b5ab74b9331cbe618bea6561aba83c4d291dc5ccc16df1d72023c462212` | Verified identical | **UNTOUCHED** |
| `WinISO26_README.md` | Must NOT exist | Does not exist (`False`) | **ABSENT** |
| `Inferno` Checkout | `D:\WinISO26\Inferno-Research\Inferno` | Clean working tree at `cc4302a99167abec69b714cfd00c38caece7e7de` | **UNTOUCHED** |

---

## 3. Campaign 194 Architecture Audit

WinIOS enters Campaign 195 with a functional synthetic kernel runtime established in Campaign 194:

1. **Boot Contract:** `SyntheticKernelLoader` validates Mach-O images and supplies a 256-byte packed `SyntheticKernelBootInfo` struct in register `X0` at EL1 (`0x4B45524E454C3031`).
2. **Physical Memory Management:** `PhysicalPageManager` allocates 16KB page frames tracked by a guest-side bitmap.
3. **Virtual Memory Management:** `VirtualPageTableManager` constructs and walks L2/L3 page tables conforming to 16KB translation granule rules.
4. **Demand Paging:** `DemandPagingManager` catches translation faults at EL1, allocates page frames, updates L3 descriptors, and resumes execution via `ERET`.
5. **Kernel Heap:** `KernelHeapAllocator` manages heap memory with boundary tags, alignment, and coalescing.
6. **Thread Control Blocks:** `GuestTcb` (512-byte packed struct) stores thread identification, scheduling state, GPRs `X0–X30`, `PC`, and stack pointers.
7. **Cooperative Context Switching:** `cpu_switch_to` emits and executes 22 ARM64 instructions saving and restoring AAPCS64 callee-saved registers (`X19–X28`, `FP`, `LR`, `SP`).
8. **Priority Scheduler:** 32 priority levels, $O(1)$ bitmap-indexed run queues, round-robin timeslicing, and idle WFI loop.
9. **Preemption Timer:** Virtual timer expiration delivering PPI 27 through GICv3, vectoring to EL1, acknowledging via `ICC_EOIR1_EL1`, and resuming via `ERET`.
10. **SMP Scheduling:** Per-core run queues, CPU affinity masks, work-stealing load balancing, and SGI 1 cross-core rescheduling.
11. **Synchronization:** Exclusive spinlocks (`LDAXR`/`STLXR`/`STLR`) and wait queues with FIFO and priority policies.
12. **Syscalls & IPC:** Lower EL synchronous exception vector (`SVC #0`), X8/X16 ABI dispatch, 64-byte `KernelMessage` mailbox queues, and capability handle tables.
13. **Device Drivers & Diagnostics:** Synthetic PL011 UART console MMIO (`0x09000000`, IRQ 33), panic records, and AAPCS64 frame pointer stack unwinding.

---

## 4. Current Context-Switch Implementation and Architecture

### 4.1 Cooperative Context Switching (`cpu_switch_to`)
- Routine: 22 instructions (88 bytes) in `src/kernel/context_switch.cpp`.
- Saved State:
  - SP (`[X0, #0x28]`)
  - LR / X30 (`[X0, #0x30]`)
  - Callee-saved registers: `X19–X20` (`#0xd8`), `X21–X22` (`#0xe8`), `X23–X24` (`#0xf8`), `X25–X26` (`#0x108`), `X27–X28` (`#0x118`), `X29–X30` (`#0x128`).
- Restored State:
  - SP from `[X1, #0x28]`
  - `X19–X28`, `FP`, `LR` from `X1` offsets
  - Target PC loaded into `X16` from `[X1, #0x30]`
  - Jump via `BR X16`
- **Limitation:** Cooperative switching assumes caller-saved registers (`X0–X18`) are scratch and do not contain live task state.

### 4.2 Current Preemptive Context Switching
- In Stage 194.10, timer preemption vectors to EL1 through the IRQ vector at `VBAR_EL1 + 0x280` or `+ 0x380`.
- The IRQ vector handler executes `guest_preemption_irq.S`, which saves temporary registers, acknowledges GICv3 `ICC_EOIR1_EL1`, and executes `ERET`.
- High-level thread switching currently relies on `KernelThread::save_context_from_cpu` and `KernelThread::load_context_to_cpu`.

---

## 5. Identified Architectural Evidence Gaps & Risks

1. **Incomplete Asynchronous Context Preservation:**
   - Preemption at an arbitrary instruction boundary can catch live application state in caller-saved registers (`X0–X18`), condition flags (`NZCV`), interrupt masks (`DAIF`), vector/SIMD registers (`Q0–Q31`), floating-point control/status (`FPCR`/`FPSR`), and thread-local pointers (`TPIDR_EL0`/`TPIDR_EL1`).
   - If an interrupt handler or scheduler modifies any of these registers without saving them in a dedicated `ExceptionFrame`, the preempted task will experience silent data corruption upon resumption.
2. **SIMD/FP Context Isolation:**
   - Neither `GuestTcb` nor `cpu_switch_to` currently saves or restores the 32 128-bit vector registers `Q0–Q31` or `FPCR`/`FPSR`.
   - Multi-task floating-point or vector operations will corrupt each other across preemption boundaries unless an extended context or exception frame preserves SIMD state.
3. **PSTATE and Condition Flag Leakage:**
   - If Task A executes a comparison (`CMP X0, #0`) setting `NZCV`, and is preempted by a timer interrupt, Task B must not alter Task A's `NZCV`. SPSR_EL1 saves PSTATE on exception entry, and `ERET` restores PSTATE, but thread switching must ensure SPSR_EL1 is correctly tracked per-thread.
4. **Thread-Local Storage Isolation:**
   - `TPIDR_EL0` and `TPIDR_EL1` are maintained in `CPU`, but are not explicitly saved or swapped during `cpu_switch_to` or preemptive context switches.
5. **Wait-Queue Race Conditions:**
   - Race scenarios (wake immediately before block, wake after block, cross-core wake) require verification under multi-thread and multicore conditions to ensure zero lost wakeups.
6. **SMP Migration Invariants:**
   - Migrating a thread from Core 0 to Core 1 must preserve the full architectural CPU context, including ASID, TLS, and floating-point registers, while guaranteeing the thread cannot be marked `Running` on both cores simultaneously.

---

## 6. Campaign 195 Acceptance Goals

Campaign 195 will systematically resolve all identified gaps:
- Complete architectural CPU context inventory and 100% GPR preservation (`X0–X30`).
- Extended SIMD/FP context preservation (`Q0–Q31`, `FPCR`, `FPSR`).
- PSTATE, DAIF, and exception return verification.
- Thread-local state (`TPIDR_EL0`, `TPIDR_EL1`) and ASID address-space isolation.
- Full asynchronous exception frame architecture on guest kernel stack.
- Arbitrary instruction boundary preemption test suite.
- Multi-core SMP migration and cross-core interrupt (SGI 1) stress validation.
- Concurrency stress on wait queues, spinlocks, demand paging, and memory allocators.
- Long-running synthetic workload corpus with deterministic replay.
