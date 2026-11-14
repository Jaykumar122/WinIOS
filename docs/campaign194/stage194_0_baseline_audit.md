# Stage 194.0 — Baseline, Evidence and Kernel Architecture Audit

## 1. Executive Summary & Baseline Identification
This document establishes the official starting baseline and architectural audit for **Campaign 194: Synthetic Kernel Boot, Memory Management, Scheduling & Kernel-Service Integration**.

- **Repository:** `D:\WinISO26\WinIOS\`
- **Branch:** `campaign-191r`
- **Baseline Git Commit:** `f0627319a0d83176702950e25356abd1bb3338b7` (Campaign 193 closeout)
- **Host System:** Windows 11 x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)
- **Secondary Toolchain:** Zig 0.17.0 (Clang 22.1.8 + LLVM Mach-O Linker)
- **Target Research Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`, ARM64 / ARM64e)
- **Baseline Test Regression:**
  - **Release:** 365 passed, 1 skipped (`inferno_integration_tests`), 0 failed (366 total targets, 100% passing rate)
  - **Debug:** 365 passed, 1 skipped (`inferno_integration_tests`), 0 failed (366 total targets, 100% passing rate)

---

## 2. Protected Files and Pinned Repositories Integrity

Prior to beginning Campaign 194, all non-negotiable file integrity checks were performed:

| Target File / Path | Expected State / Hash | Verified State / Hash | Status |
| :--- | :--- | :--- | :--- |
| `D:\WinISO26\WinIOS_README.md` | `61f4d078d5071b5ab16093eb5418ed15afb7ed41cd3c84f080ce44e196ea7fef` | Verified identical | **UNTOUCHED** |
| `D:\WinISO26\WinIOS\docs\progress.md` | `447e7b5ab74b9331cbe618bea6561aba83c4d291dc5ccc16df1d72023c462212` | Verified identical | **UNTOUCHED** |
| `D:\WinISO26\WinISO26_README.md` | Must NOT exist | Does not exist (`False`) | **ABSENT** |
| `D:\WinISO26\Inferno-Research\Inferno` | Pinned commit `cc4302a99167abec69b714cfd00c38caece7e7de` | Verified clean working tree, commit `cc4302a` | **UNTOUCHED** |

---

## 3. Existing Synthetic Kernel Artifact State

From Campaign 193 Stage 193.16:
- **Artifact:** `tests/artifacts/bin/synthetic_el1_kernel`
- **Source:** `tests/artifacts/src/synthetic_el1_kernel.S`
- **File Format:** 64-bit Mach-O (`MH_EXECUTE`, `CPU_TYPE_ARM64`, `PLATFORM_MACOS`, linked with `-pagezero_size 0`)
- **File Size:** 51,832 bytes
- **SHA-256:** `ef08793bdf38817e06c8d37157d5b25ba94b959269a7965ad45a82d4c4a200cf`
- **Classification:** `SYNTHETIC_KERNEL`
- **Current Behavior:**
  - Vector table aligned to 2KB at `vector_table`.
  - Kernel entry initializes `VBAR_EL1`, `SP` (EL1), and `SP_EL0`.
  - `ERET` drops privilege from EL1 to EL0 (`_client_entry`).
  - Client triggers `SVC #0` handled at `VBAR_EL1 + 0x400` (Lower EL AArch64 Sync).
  - Handles diagnostic print, counter increment, client exit, and synthetic data abort logging.

---

## 4. Guest-versus-Host Execution Ownership Matrix

In Campaign 194, operating system responsibilities must be explicitly partitioned into guest-executed code versus host-provided infrastructure:

| Subsystem | Current Ownership (Campaign 193 Baseline) | Campaign 194 Target Ownership | Classification Target |
| :--- | :--- | :--- | :--- |
| **CPU Instruction Decode & Execution** | Host C++ `CPU::step` | Host C++ `CPU::step` | `HOST_MODEL` |
| **Memory Backing Store** | Host C++ `Memory` (contiguous RAM) | Host C++ `Memory` (physical DRAM backing) | `HOST_MODEL` |
| **Physical Page Allocation** | Host `Memory` / test harness offsets | Guest EL1 code (free-page bitmap / linked list) | `GUEST_MEMORY_MANAGER` |
| **Virtual Page Table Construction** | Host C++ test harness (`mem.write_u64`) | Guest EL1 code (page-table walker / entry updater) | `GUEST_MEMORY_MANAGER` |
| **Demand Page Fault Handling** | Host test setup + basic guest Far logging | Complete guest EL1 page-fault resolution & TLBI | `GUEST_MEMORY_MANAGER` |
| **Kernel Heap Allocation** | None (static memory slots) | Guest EL1 heap allocator (`kmalloc`/`kfree`) | `GUEST_MEMORY_MANAGER` |
| **Thread Control Blocks (TCBs)** | None in guest (Darwin userspace host `ThreadContext`) | Guest EL1 thread structures & states | `GUEST_SCHEDULER` |
| **CPU Context Switching** | Host C++ register reassignment | Guest EL1 register save/restore assembly routine | `GUEST_SCHEDULER` |
| **Task Scheduling & Queues** | Host C++ `SMPCluster` round-robin | Guest EL1 priority ready queues & idle task | `GUEST_SCHEDULER` |
| **Timer Preemption** | GIC PPI 27 delivered to host loop | Guest EL1 timer handler saves context & switches | `GUEST_SCHEDULER` |
| **Kernel Synchronization** | Host C++ `std::mutex` / atomic | Guest EL1 spinlocks / wait queues | `GUEST_EL1` |
| **Syscall Dispatching** | Host `DarwinUserspaceEngine` intercept | Guest EL1 syscall table & argument validator | `GUEST_SYSCALL` |
| **Kernel IPC Message Queues** | Host `MachIpcSubsystem` | Guest EL1 message queues & blocking wake-up | `GUEST_EL1` |
| **Kernel Object Handles** | Host table handles | Guest EL1 handle table & refcounting | `GUEST_EL1` |
| **Device Driver Interface** | Host MMIO device simulation | Guest EL1 driver issuing MMIO loads/stores | `GUEST_EL1` |

---

## 5. Architectural Boundaries and Limitations

1. **Synthetic Kernel Scope vs Genuine Apple XNU:**
   - WinIOS synthetic kernel runtime is an independently compiled ARM64 freestanding kernel designed to test and execute OS primitives inside the emulator.
   - It does NOT claim, attempt, or require authentic Apple XNU kernel boot from encrypted Apple IPSWs.
2. **Deterministic Replay Guarantee:**
   - All guest timer and scheduler operations must remain 100% deterministic, without depending on unpredictable host thread timing or asynchronous wall-clock race conditions.
3. **No Dual MMUs:**
   - The established 16KB Stage-1 MMU implementation in `src/cpu/mmu.h` and `src/cpu/mmu.cpp` remains the single authoritative translation engine. Guest code interacts with it by manipulating valid Stage-1 translation descriptors and issuing `TLBI` instructions.

---

## 6. Acceptance Verification
- Baseline audit complete.
- All non-negotiable repository safety constraints verified.
- Initial baseline test suite confirmed 100% green (365/366 passed, 1 skipped).
- Ready to proceed to **Stage 194.1**.
