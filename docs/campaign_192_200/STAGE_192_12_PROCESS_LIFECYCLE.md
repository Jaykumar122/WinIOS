# WinIOS Campaign 192 — Stage 192.12: Runtime Initialization and Process Exit Report

**Stage:** 192.12  
**Title:** Runtime Initialization, Stack Frame Construction and Process Exit Conformance  
**Date:** 2026-11-13  
**Status:** COMPLETE (4/4 tests passed)  
**Host:** Windows 11 x64 | MSVC C++20 (`/W4 /WX /permissive-`)  
**Target:** ARM64 / Darwin Userspace (EL0)

---

## 1. Executive Summary

Stage 192.12 verifies Darwin userspace runtime initialization and process termination conformance. In Darwin ARM64 userspace (`LC_MAIN`), the kernel constructs an initial stack frame containing the argument count (`argc`), pointer vector arrays (`argv`, `envp`, `apple`), and the string payloads placed at high addresses. In addition, static initializers in `__mod_init_func` sections must execute in bottom-up topological dependency order (leaf library -> intermediate library -> main binary) prior to transfer of control to `main()`. Finally, process exit via Darwin `SYS_exit` (syscall 1, code in `X0`) must halt guest CPU retirement immediately and record the process exit code without runaway execution.

All 4 test cases passed cleanly.

---

## 2. Darwin ABI Stack Frame & Process Lifecycle Conformance

### 2.1 Stack Frame Construction
Verified `UserspaceMachOLoader::construct_initial_stack`:
1. High-address memory allocation for payload strings: `apple[0]` (executable path), followed by `envp` string payloads, followed by `argv` string payloads.
2. Word-alignment down to 8 bytes, followed by allocation of pointer vector slots:
   $$\text{slots} = 1 + N_{\text{argv}} + 1 + M_{\text{envp}} + 1 + K_{\text{apple}} + 1$$
3. 16-byte stack alignment enforcement (`SP &= ~0xFULL`) in accordance with AAPCS64.
4. Correct layout at `SP`:
   - `[SP + 0]`: `argc` (64-bit value $N_{\text{argv}}$)
   - `[SP + 8 .. SP + 8*N]`: `argv[0]` through `argv[N-1]` pointers
   - `[SP + 8*(N+1)]`: `NULL` terminator
   - `[SP + 8*(N+2) ..]`: `envp[0]` through `envp[M-1]` pointers
   - `NULL` terminator
   - `apple[0]` through `apple[K-1]` pointers
   - `NULL` terminator

### 2.2 Mod-Init Constructor Ordering
Verified that static initializers declared via `__DATA,__mod_init_func` adhere to strict topological dependency order:
1. Leaf dylibs execute first (providing basic runtime / utility facilities).
2. Intermediate dylibs execute second.
3. Main binary initializers execute last before `main()`.

### 2.3 Process Exit Semantics
Verified `SYS_exit` (syscall 1) handling in `DarwinUserspaceEngine`:
- Exit code supplied in guest register `X0`.
- Process transitioned to `Zombie` / `Terminated` in `DarwinBsdSubsystem`.
- Engine terminates execution loop immediately, halting guest CPU step iteration.
- Tested exit codes: 0 (clean success), 42 (arbitrary non-zero), 255 (error code).

---

## 3. Test Verification Results

Target: `stage_192_12_process_lifecycle_tests`

| Test Case | Description | Result |
| :--- | :--- | :--- |
| **Test 1** | Stack Frame Layout & Pointer Vector Conformance | **PASS** |
| **Test 2** | Stack SP Alignment Invariants Across Diverse Arg Counts (0..9 args, 0..3 env) | **PASS** |
| **Test 3** | Mod-Init Constructor Execution Order (Leaf -> Mid -> Root) | **PASS** |
| **Test 4** | Process Exit Semantics via SYS_exit (Codes 0, 42, 255) | **PASS** |

All 4 test cases passed with zero regressions.
