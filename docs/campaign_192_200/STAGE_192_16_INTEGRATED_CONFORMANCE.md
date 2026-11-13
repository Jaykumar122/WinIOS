# WinIOS Campaign 192 — Stage 192.16: Integrated Darwin Userspace Conformance Report

**Stage:** 192.16  
**Title:** Integrated Darwin Userspace Conformance and Multi-Subsystem Interoperability  
**Date:** 2026-11-13  
**Status:** COMPLETE (4/4 tests passed)  
**Host:** Windows 11 x64 | MSVC C++20 (`/W4 /WX /permissive-`)  
**Target:** ARM64 / Darwin Userspace (EL0)

---

## 1. Executive Summary

Stage 192.16 demonstrates the seamless interoperability and simultaneous collaboration of all Darwin userspace subsystems within a single execution runtime:
1. **Darwin BSD VFS Pipeline**: Inter-process pipe allocation (`SYS_pipe`), synchronized stream writing (`SYS_write`), descriptor duplication (`SYS_dup`), stream reading (`SYS_read`), and cleanup (`SYS_close`).
2. **Virtual Memory & MMU**: 16 KB page granule anonymous mapping (`SYS_mmap`), memory advisory signaling (`SYS_madvise`), cross-page data read/write verification, and unmapping (`SYS_munmap`).
3. **Mach IPC Subsystem**: Multi-task port space isolation, port rights ownership (`Receive` and `Send`), kernel message delivery (`mach_msg`), and body payload preservation.
4. **Thread & TLS Lifecycle**: Concurrent thread registration, 16-byte stack pointer alignment, isolated `TPIDR_EL0` userspace thread pointers, and lossless general register preservation across context switches (`switch_thread`).

All 4 integration scenarios passed with zero warnings and zero regressions.

---

## 2. Integrated Scenarios & Verified Behavioral Invariants

### 2.1 Scenario 1: VFS Pipe Pipeline
- Allocated anonymous kernel FIFO pipe generating separate read descriptor (`rfd = 3`) and write descriptor (`wfd = 4`).
- Streamed 38-byte payload into `wfd`.
- Duplicated `rfd` via `SYS_dup` yielding lowest-available slot `dup_rfd = 5`.
- Read from `dup_rfd` verifying exact byte transmission.
- Closed descriptors with proper FIFO ref-counting.

### 2.2 Scenario 2: Virtual Memory & Madvise
- Allocated 32 KB (two 16 KB pages) at `0x400000` with `PROT_READ | PROT_WRITE`.
- Verified memory reads and writes across both page boundaries.
- Dispatched `SYS_madvise` with `MADV_WILLNEED` returning success (`ret0 = 0`, `C = 0`).
- Deallocated entire region via `SYS_munmap`.

### 2.3 Scenario 3: Mach IPC Messaging
- Server task (PID 1603) and Client task (PID 1604) established distinct port spaces.
- Server allocated service port (`kMachPortRightReceive`).
- Client acquired send right and allocated reply port.
- Dispatched request message via `mach_msg(kMachSendMsg)` and successfully received via `mach_msg(kMachRcvMsg)` in server space with body intact.

### 2.4 Scenario 4: Thread Concurrency & TLS Isolation
- Configured thread 1 (`TPIDR_EL0 = 0x100000`, `X0 = 0xAAAA`).
- Configured thread 2 (`TPIDR_EL0 = 0x200000`, `X0 = 0xBBBB`).
- Alternated context switches between Thread 1 and Thread 2, confirming that `TPIDR_EL0` and register state remain completely isolated.

---

## 3. Test Verification Results

Target: `stage_192_16_integrated_conformance_tests`

| Test Case | Description | Result |
| :--- | :--- | :--- |
| **Test 1** | Integrated VFS Pipeline (`pipe`, `write`, `dup`, `read`, `close`) | **PASS** |
| **Test 2** | Integrated Virtual Memory & MMU Madvise (`mmap`, `madvise`, `munmap`) | **PASS** |
| **Test 3** | Integrated Mach IPC Messaging & Task Port Space (`mach_msg`, send/recv rights) | **PASS** |
| **Test 4** | Integrated Threads & TLS Context Switching (`tpidr_el0`, multi-TID register state) | **PASS** |

All 4 tests passed cleanly.
