# Campaign 194 — Evidence Matrix

## 1. Evidence Classification Taxonomy

In strict accordance with WinIOS Campaign 194 governance rules, every technical capability and empirical test assertion is classified into one of the following authoritative categories:

1. **`HOST_MODEL`**: Host C++ simulation, test harness fixture, or boundary validator.
2. **`HOST_SERVICE`**: Host-provided service exposed to guest via an architectural interface.
3. **`GUEST_EL0`**: Authentic ARM64 machine instructions executed in unprivileged user mode (EL0).
4. **`GUEST_EL1`**: Authentic ARM64 machine instructions executed in privileged kernel mode (EL1).
5. **`GUEST_SYSCALL`**: Architectural exception delivery (`SVC #0` -> Lower EL Synchronous vector -> EL1 dispatch -> `ERET`).
6. **`GUEST_SCHEDULER`**: Priority, preemptive, or SMP scheduling algorithms selecting and switching guest threads.
7. **`GUEST_MEMORY_MANAGER`**: Physical and virtual memory allocation, page table manipulation, and fault handling in guest code.
8. **`SYNTHETIC_KERNEL`**: Independently compiled freestanding ARM64 Mach-O kernel runtime running in WinIOS.
9. **`APPLE_KERNEL`**: Authentic Apple XNU / Darwin kernel execution. **NEVER FALSELY CLAIMED / NOT IMPLEMENTED**.

---

## 2. Evidence Mapping Table

| Stage | Capability / Subsystem | Primary Classification | Evidence & Verifier |
| :--- | :--- | :--- | :--- |
| **194.0** | Baseline, Evidence & Architecture Audit | `HOST_MODEL` | 365 passed / 1 skipped in Debug & Release |
| **194.1** | Kernel Image Loading & Entry Contract (X0, 256B) | `SYNTHETIC_KERNEL` / `GUEST_EL1` | `stage194_1_kernel_boot_contract_tests` (5/5 pass) |
| **194.2** | Canonical Virtual Memory Layout (16KB granule) | `GUEST_MEMORY_MANAGER` | `stage194_2_kernel_memory_layout_tests` (5/5 pass) |
| **194.3** | Guest Physical Allocator (16KB page bitmap) | `GUEST_MEMORY_MANAGER` / `GUEST_EL1` | `stage194_3_physical_allocator_tests` (KERNEL-002: 113 retired) |
| **194.4** | Virtual Page Table Manager (L2/L3 table walker) | `GUEST_MEMORY_MANAGER` / `GUEST_EL1` | `stage194_4_virtual_page_table_tests` (5/5 pass) |
| **194.5** | Demand Paging & Page-Fault Resolution | `GUEST_MEMORY_MANAGER` / `GUEST_EL1` | `stage194_5_demand_paging_tests` (5/5 pass, ERET return) |
| **194.6** | Kernel Heap Allocator (`kmalloc`/`kfree`) | `GUEST_MEMORY_MANAGER` | `stage194_6_kernel_heap_tests` (6/6 pass, boundary tags) |
| **194.7** | Thread Control Blocks & Lifecycle (512B TCB) | `GUEST_SCHEDULER` | `stage194_7_thread_lifecycle_tests` (6/6 pass) |
| **194.8** | Guest CPU Context Switching (`cpu_switch_to`) | `GUEST_SCHEDULER` / `GUEST_EL1` | `stage194_8_context_switch_tests` (KERNEL-003: 22 retired) |
| **194.9** | Priority Scheduler & Run Queues (32 priorities) | `GUEST_SCHEDULER` | `stage194_9_guest_scheduler_tests` (7/7 pass, O(1) bitmap) |
| **194.10**| Preemptive Scheduling & Timer Interrupts (PPI 27) | `GUEST_SCHEDULER` / `GUEST_EL1` | `stage194_10_preemption_tests` (KERNEL-004: 32 retired) |
| **194.11**| SMP Scheduling, Affinity & Work Stealing (SGI 1) | `GUEST_SCHEDULER` | `stage194_11_smp_scheduler_tests` (6/6 pass, 8 cores) |
| **194.12**| Kernel Synchronization (Spinlocks & Wait Queues) | `GUEST_EL1` | `stage194_12_sync_primitives_tests` (62 assertions, LDAXR/STLXR) |
| **194.13**| Guest Syscall Entry & Dispatch (SVC #0) | `GUEST_SYSCALL` / `GUEST_EL1` | `stage194_13_syscall_dispatch_tests` (30 assertions, ERET return) |
| **194.14**| Kernel IPC & Message Queues (64B KernelMessage) | `GUEST_EL1` | `stage194_14_kernel_ipc_tests` (60 assertions, channel ports) |
| **194.15**| Capability Handle Table & Rights Attenuation | `HOST_MODEL` / `GUEST_EL1` | `stage194_15_handle_management_tests` (43 assertions) |
| **194.16**| Device Driver Interface & Console MMIO (PL011) | `HOST_SERVICE` / `GUEST_EL1` | `stage194_16_device_drivers_tests` (31 assertions, 0x09000000) |
| **194.17**| Kernel Diagnostics, Panic & Stack Unwinder | `GUEST_EL1` | `stage194_17_kernel_diagnostics_tests` (29 assertions, AAPCS64) |
| **194.18**| Synthetic Kernel Workload Corpus | `SYNTHETIC_KERNEL` / `GUEST_EL1` | `stage194_18_workload_corpus_tests` (21 assertions, 307 retired) |
| **194.19**| Full Regression & Evidence Conformance Audit | `HOST_MODEL` | 384/384 CTest targets pass (100% passing rate) |

---

## 3. Provenance and Authenticity Invariants

1. **Independent Cross-Compilation:**
   All 6 guest kernel artifacts (`guest_physical_allocator`, `guest_context_switch`, `guest_preemption_irq`, `guest_kernel_smp_threads`, `guest_kernel_page_walk`, `guest_kernel_ipc_pingpong`) are assembled and linked via Zig 0.17.0 (`clang`/`lld`) with `-target aarch64-macos -nostdlib -Wl,-pagezero_size,0`.
2. **Instruction Retirement Verification:**
   Every test harness measures actual instructions executed and retired through `cpu.step(mem)` rather than host stub mock values.
3. **Absence of Proprietary Apple IP:**
   WinIOS does not incorporate Apple proprietary keys, Secure Enclave microcode, or leaked XNU binaries. All synthetic kernel code is independently engineered.
