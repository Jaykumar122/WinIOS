# WinIOS Campaign 203 Stage 203.20: Synthetic Darwin Syscall Dispatcher Report

**Date:** 2026-11-16  
**Status:** COMPLETE (PASS)  
**Target Device Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Environment:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`)  
**Test Suite:** `stage203_20_darwin_syscall_dispatcher_tests` (Release & Debug)

---

## 1. Executive Summary

Stage 203.20 establishes a synthetic Darwin syscall and Mach trap dispatcher that integrates the VM object model (`DarwinVmSpace`) and Mach IPC subsystem (`MachIpcSubsystem`) into a unified guest execution interface.

Key architectural features verified:
1. **Unified Routing on `SVC #0x80`:** In ARM64 Darwin, `SVC #0x80` handles both BSD syscalls and Mach traps. Positive `X16` values represent BSD syscall numbers, while negative `X16` values represent Mach traps (e.g., `-26` for `mach_reply_port`, `-31` for `mach_msg`). The dispatcher dispatches negative `X16` directly to registered Mach trap handlers.
2. **BSD Core Syscalls:** Implemented and tested `SYS_getpid` (20), `SYS_getuid` (24), `SYS_write` (4), and `SYS_exit` (1). Standard output written to file descriptor 1 is captured in the dispatcher's stdout buffer.
3. **Bound `DarwinVmSpace` Integration:** Wired `SYS_mmap` (197), `SYS_mprotect` (74), and `SYS_munmap` (73) to operate directly upon the bound guest virtual address space.
4. **Bound `MachIpcSubsystem` Integration:** Wired `mach_reply_port` (-26), `mach_task_self` (-28), `mach_host_self` (-29), and `mach_msg` / `mach_msg_overwrite` (-31 / -32) to the bound IPC subsystem with the invoking task ID.
5. **Architectural Error Reporting:** Unregistered BSD syscalls cleanly set guest register `X0` to `ENOSYS` (78) and return `-ENOSYS`. Unregistered Mach traps cleanly set `X0` to `KERN_FAILURE` (5) and return `KERN_FAILURE`.

---

## 2. Test Execution Matrix

| Test Case | Description | Result (Release) | Result (Debug) |
|---|---|---|---|
| `test_bsd_basic_syscalls` | BSD `getpid`, `getuid`, `write`, `exit` behavior and stdout capture | PASS | PASS |
| `test_bound_vm_space_syscalls` | Direct `mmap`, `mprotect`, `munmap` execution on bound `DarwinVmSpace` | PASS | PASS |
| `test_bound_mach_ipc_traps` | `mach_reply_port`, `mach_task_self`, `mach_host_self`, `mach_msg` routing | PASS | PASS |
| `test_negative_x16_routing` | Unified routing for negative `X16` via `SVC #0x80` vector | PASS | PASS |
| `test_unimplemented_syscall_error_reporting` | `ENOSYS` and `KERN_FAILURE` reporting on invalid syscall / trap numbers | PASS | PASS |

---

## 3. Verification Details

All tests compiled under `/W4 /WX /permissive-` with 0 warnings and executed with 0 failures in both Release and Debug configurations.
