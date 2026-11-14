# Guest Versus Host Ownership Audit

## 1. Objective and Audit Scope

This document provides a comprehensive, rigorous classification of ownership across all kernel, CPU, memory, scheduling, and device subsystems in WinIOS for **Campaign 195**.

To prevent misleading claims of guest capability, every component is analyzed across:
- **Guest Source Code:** Code compiled into ARM64 machine instructions and executed by `CPU::step`.
- **Host Source Code:** C++ code compiled by MSVC and running natively on the Windows host.
- **Guest-Owned State:** Data structures residing directly within guest physical RAM (e.g. page tables, TCBs, spinlock words).
- **Host-Owned State:** Data structures residing in host memory (e.g. `std::vector`, host thread managers, device mocks).
- **Entry Point & Execution Level:** Architectural vector, function address, and privilege level (`EL0` vs `EL1`).
- **Host Callbacks & Intercepts:** Any point where guest execution transitions into or queries host logic.
- **Test Evidence:** Specific unit tests verifying the component.

---

## 2. Detailed Component Ownership Classification

### 2.1 Physical Page Allocation
- **Guest Implementation:** `tests/artifacts/src/guest_physical_allocator.S` (compiled via Zig 0.17.0 into `tests/artifacts/bin/guest_physical_allocator`, 50,888 B, SHA `b51b7c0cc...`).
- **Host Implementation:** `src/memory/physical_page_manager.h`, `src/memory/physical_page_manager.cpp` (`PhysicalPageManager`).
- **Guest-Owned State:** 16KB physical memory bitmap stored in guest RAM at base `0x40000000`, page status bits.
- **Host-Owned State:** `PhysicalPageManager` configuration (`ram_base_`, `ram_size_`, `total_pages_`).
- **Entry Point / Privilege:** `_start` / `alloc_page` at `GUEST_EL1`.
- **Classification:** Genuine `GUEST_MEMORY_MANAGER` and `GUEST_EL1` for bitmap operations; `HOST_MODEL` provides bootstrap configuration and boundary assertion in test fixtures.
- **Test Evidence:** `stage194_3_physical_allocator_tests.cpp` (113 guest instructions retired).

### 2.2 Virtual Page Table Walking & Descriptor Management
- **Guest Implementation:** `tests/artifacts/src/guest_kernel_page_walk.S` (compiled via Zig 0.17.0 into `tests/artifacts/bin/guest_kernel_page_walk`, 50,152 B, SHA `938fbffca...`).
- **Host Implementation:** `src/memory/virtual_page_table.h`, `src/memory/virtual_page_table.cpp` (`VirtualPageTableManager`), `src/cpu/mmu.cpp`.
- **Guest-Owned State:** L2 and L3 translation tables formatted in 16KB granule descriptors in guest RAM (`TTBR0_EL1` / `TTBR1_EL1`).
- **Host-Owned State:** Page table manager host helpers, memory bounds checkers.
- **Entry Point / Privilege:** `_start` / `walk_table` at `GUEST_EL1`.
- **Classification:** `GUEST_MEMORY_MANAGER` for guest table walks; `HOST_MODEL` simulates hardware MMU table translation during `cpu.step()`.
- **Test Evidence:** `stage194_4_virtual_page_table_tests.cpp`, `stage194_18_workload_corpus_tests.cpp` (33 guest instructions retired).

### 2.3 Kernel Heap Allocation (`kmalloc` / `kfree`)
- **Guest Implementation:** Can be called via syscall / function call at EL1.
- **Host Implementation:** `src/memory/kernel_heap_allocator.h`, `src/memory/kernel_heap_allocator.cpp` (`KernelHeapAllocator`).
- **Guest-Owned State:** Heap blocks bounded by 16-byte `BlockHeader` structures with magic `0x48454150` inside guest physical heap range `0x42000000–0x43000000`.
- **Host-Owned State:** Free list linked pointer cache for host inspection.
- **Entry Point / Privilege:** `kmalloc` / `kfree` at `GUEST_EL1`.
- **Classification:** `GUEST_MEMORY_MANAGER` (block headers and payload exist in guest RAM); currently driven by host C++ helper in `KernelHeapAllocator`.
- **Test Evidence:** `stage194_6_kernel_heap_tests.cpp`.

### 2.4 CPU Context Switching (`cpu_switch_to`)
- **Guest Implementation:** `src/kernel/context_switch.cpp` (`emit_guest_switch_routine`), `tests/artifacts/src/guest_context_switch.S` (compiled via Zig 0.17.0 into `tests/artifacts/bin/guest_context_switch`, 50,088 B, SHA `651bd7ed4...`).
- **Host Implementation:** `ContextSwitch::switch_threads`, `KernelThread::save_context_from_cpu`, `KernelThread::load_context_to_cpu`.
- **Guest-Owned State:** Callee-saved register save area inside `GuestTcb` (`[X0, #0xd8]` through `[X0, #0x128]`), stack pointers `SP_EL0` / `SP_EL1`, and return PC.
- **Host-Owned State:** Thread descriptor bookkeeping in `KernelThreadManager`.
- **Entry Point / Privilege:** `cpu_switch_to` entry address at `GUEST_EL1`.
- **Classification:** Genuine `GUEST_CONTEXT_SWITCH` and `GUEST_EL1`. The 22-instruction switch executes directly on virtual ARM64 registers.
- **Test Evidence:** `stage194_8_context_switch_tests.cpp` (22 guest instructions retired).

### 2.5 Thread Representation and Lifecycle
- **Guest Implementation:** `GuestTcb` struct (512-byte packed struct) stored in guest memory.
- **Host Implementation:** `KernelThread`, `KernelThreadManager` in `src/kernel/thread.h`, `src/kernel/thread.cpp`.
- **Guest-Owned State:** Magic `0x5443425F54485244`, TID, execution state, priority, timeslice, GPR save area `gpr[31]`, stack bases.
- **Host-Owned State:** `std::vector<std::unique_ptr<KernelThread>>` thread table.
- **Entry Point / Privilege:** Managed via host C++ API; consumed by guest scheduler.
- **Classification:** `HOST_SERVICE` managing `GUEST_SCHEDULER` state.
- **Test Evidence:** `stage194_7_thread_lifecycle_tests.cpp`.

### 2.6 Priority Scheduling & Run Queues
- **Guest Implementation:** Guest scheduler dispatch routines in guest workloads.
- **Host Implementation:** `src/kernel/scheduler.h`, `src/kernel/scheduler.cpp` (`GuestPriorityScheduler`).
- **Guest-Owned State:** `ready_bitmap_` (32 priority levels), run-queue heads/tails, thread states in `GuestTcb`.
- **Host-Owned State:** Queue structure wrappers and timer tick dispatch.
- **Entry Point / Privilege:** Invoked upon timer interrupt or voluntary yield at `GUEST_EL1`.
- **Classification:** `GUEST_SCHEDULER`.
- **Test Evidence:** `stage194_9_guest_scheduler_tests.cpp`.

### 2.7 Hardware Timer Preemption
- **Guest Implementation:** `tests/artifacts/src/guest_preemption_irq.S` (compiled via Zig 0.17.0 into `tests/artifacts/bin/guest_preemption_irq`, 50,008 B, SHA `9836f774a...`).
- **Host Implementation:** `src/kernel/preemption_timer.h`, `src/kernel/preemption_timer.cpp` (`PreemptionTimerManager`), `src/cpu/gicv3.cpp`.
- **Guest-Owned State:** Vector table entry at `VBAR_EL1 + 0x280` / `0x380`, stack frame, `ICC_EOIR1_EL1` write.
- **Host-Owned State:** Simulated hardware timer tick generator, GICv3 distributor state.
- **Entry Point / Privilege:** EL1 IRQ Exception Vector (`GUEST_INTERRUPT` -> `GUEST_EL1`).
- **Classification:** `GUEST_PREEMPTION` and `GUEST_INTERRUPT`.
- **Test Evidence:** `stage194_10_preemption_tests.cpp` (32 guest instructions retired).

### 2.8 Synchronization Primitives (Spinlocks & Wait Queues)
- **Guest Implementation:** Guest exclusive load/store instructions (`LDAXR`, `STLXR`, `STLR`) emitted and executed in guest RAM; `tests/artifacts/src/guest_kernel_smp_threads.S`.
- **Host Implementation:** `src/kernel/sync.h`, `src/kernel/sync.cpp` (`KernelSpinlock`, `WaitQueue`).
- **Guest-Owned State:** 16-byte `GuestSpinlockData` in guest memory (`magic`, `lock_word`, `owner_tid`, `contention_count`).
- **Host-Owned State:** Contention profiling, deadlock detection timeouts.
- **Entry Point / Privilege:** `spin_lock` / `spin_unlock` at `GUEST_EL1`.
- **Classification:** `GUEST_EL1` execution of ARM64 synchronization instructions.
- **Test Evidence:** `stage194_12_sync_primitives_tests.cpp`, `stage194_18_workload_corpus_tests.cpp` (155 guest instructions retired).

### 2.9 System Call Entry and Dispatch
- **Guest Implementation:** Guest `SVC #0` instruction, user parameters in `X0–X5` and `X8`/`X16`. Vector stub saves context and executes `ERET`.
- **Host Implementation:** `src/kernel/syscall_dispatcher.h`, `src/kernel/syscall_dispatcher.cpp` (`GuestSyscallDispatcher`).
- **Guest-Owned State:** Syscall arguments in GPRs, vector table at `VBAR_EL1 + 0x400`, user return PC in `ELR_EL1`.
- **Host-Owned State:** Syscall number dispatch table and host emulation handlers.
- **Entry Point / Privilege:** Lower EL Synchronous vector entry (`GUEST_EL0` -> `GUEST_SYSCALL` -> `GUEST_EL1`).
- **Classification:** `GUEST_SYSCALL`.
- **Test Evidence:** `stage194_13_syscall_dispatch_tests.cpp`.

### 2.10 Inter-Process Communication (IPC)
- **Guest Implementation:** `tests/artifacts/src/guest_kernel_ipc_pingpong.S` (compiled via Zig 0.17.0 into `tests/artifacts/bin/guest_kernel_ipc_pingpong`, 50,424 B, SHA `1b87c7bd5...`).
- **Host Implementation:** `src/kernel/ipc.h`, `src/kernel/ipc.cpp` (`IpcManager`, `IpcPort`).
- **Guest-Owned State:** 64-byte `KernelMessage` structures in guest memory (`msg_id`, `sender_tid`, `receiver_port`, `payload_size`, `payload[32]`).
- **Host-Owned State:** Port lookup dictionary, thread wait queues.
- **Entry Point / Privilege:** Send/receive primitives at `GUEST_EL1`.
- **Classification:** `GUEST_EL1` / `HOST_SERVICE`.
- **Test Evidence:** `stage194_14_kernel_ipc_tests.cpp`, `stage194_18_workload_corpus_tests.cpp` (119 guest instructions retired).

### 2.11 Multicore SMP Scheduling
- **Guest Implementation:** Per-core execution in guest workloads, inter-processor interrupt via GICv3 `ICC_SGI1R_EL1`.
- **Host Implementation:** `src/kernel/smp_scheduler.h`, `src/kernel/smp_scheduler.cpp` (`SmpScheduler`).
- **Guest-Owned State:** Core run queues, thread affinity bitmasks, TCB core affinity.
- **Host-Owned State:** Multi-core dispatch loop, load balancing heuristics.
- **Entry Point / Privilege:** `schedule_core` at `GUEST_EL1`.
- **Classification:** `GUEST_SMP` and `GUEST_SCHEDULER`.
- **Test Evidence:** `stage194_11_smp_scheduler_tests.cpp`, `stage194_18_workload_corpus_tests.cpp`.

---

## 3. Audit Findings: Test-Only & Host-Assisted Paths

The audit identified three key host-assisted paths requiring hardening in Campaign 195:

1. **Host-Assisted Context Save/Restore in Preemption:**
   - In Stage 194.10, the preemption test relied on `KernelThread::save_context_from_cpu` and `load_context_to_cpu` to snapshot and restore full CPU registers, while the guest vector stub (`guest_preemption_irq.S`) only saved scratch registers and executed EOI.
   - *Campaign 195 Requirement:* Provide a complete guest-side `ExceptionFrame` on the kernel stack that saves `X0–X30`, `PC`, `PSTATE`, `SIMD/FP`, and `TLS` directly in guest memory.
2. **SIMD/Vector Register Omission:**
   - `GuestTcb` has no storage for `Q0–Q31` or `FPCR`/`FPSR`. Host context switch functions do not save or restore `v_registers_`.
   - *Campaign 195 Requirement:* Establish an extended architectural context and `ExceptionFrame` that includes full SIMD/FP state.
3. **PSTATE / NZCV / DAIF Persistence:**
   - Host `save_context_from_cpu` preserves `spsr_el1` but does not preserve individual `NZCV` condition codes if the switch occurs at EL1 without an exception frame.
   - *Campaign 195 Requirement:* Ensure `PSTATE` (including `NZCV` and `DAIF`) is preserved across both cooperative and asynchronous transitions.

---

## 4. Authenticity Certification

WinIOS does **NOT** simulate genuine Apple iOS `kernelcache` binaries or claim `APPLE_KERNEL` status. All kernel operations are executed either by freestanding ARM64 guest instructions (`GUEST_EL1`) compiled via Zig 0.17.0 or by documented host synthetic kernel services (`HOST_SERVICE` / `HOST_MODEL`).
