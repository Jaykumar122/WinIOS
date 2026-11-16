# WinIOS Campaign 203 Stage 203.21: Process Startup and Initial Thread State Report

**Date:** 2026-11-16  
**Status:** COMPLETE (PASS)  
**Target Device Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Environment:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`)  
**Test Suite:** `stage203_21_process_startup_tests` (Release & Debug)

---

## 1. Executive Summary

Stage 203.21 establishes and validates the Darwin ARM64 userspace process startup mechanism, stack frame formatting, and initial thread register configuration in `DarwinProcessStartup`.

Key architectural features verified:
1. **ARM64 Darwin Stack Layout:** Sets up the standard userspace stack:
   - `[SP]` holds 64-bit `argc`.
   - `[SP + 8]` points to `argv[0]`, followed by pointers to all arguments, terminated with `NULL`.
   - Following `argv`, `envp[0]` through `envp[M-1]` pointer slots, terminated with `NULL`.
   - Following `envp`, `apple[0]` through `apple[K-1]` pointer slots, terminated with `NULL`.
   - String contents are placed in high addresses below `stack_top` and referenced by pointer slots.
2. **16-byte Stack Pointer Alignment:** Complies strictly with the ARM64 AAPCS64 / Darwin ABI requirement that `SP` must remain 16-byte aligned at all public procedure interfaces.
3. **Guest Register Initialization:**
   - `SP` initialized to 16-byte aligned stack top.
   - `PC` set to guest executable entry point.
   - `X0` loaded with `argc`.
   - `X1` loaded with pointer to `argv` table on the stack.
   - `X2` loaded with pointer to `envp` table on the stack.
   - `X3` loaded with pointer to `apple` table on the stack.
4. **Darwin `apple[]` Dictionary:** Automatically populates `executable_path`, `stack_guard` canary, `malloc_entropy`, and `pfz` (pagezero) entries when no custom apple entries are specified.
5. **High Canonical Address Translation:** Transparently handles canonical 48-bit userspace addresses (e.g., `0x0000700000080000ULL`) mapped into physical RAM buffers.
6. **Robust Validation:** Kernel-space stack pointers (high-bit set), zero stack top, and buffer exhaustion conditions are caught and rejected cleanly.

---

## 2. Test Execution Matrix

| Test Case | Description | Result (Release) | Result (Debug) |
|---|---|---|---|
| `test_standard_launchd_startup` | Launchd default startup stack, 16-byte SP alignment, registers X0-X3, memory layout | PASS | PASS |
| `test_custom_arguments_and_apple_dict` | Multi-arg command line, custom environment, and custom apple vector elements | PASS | PASS |
| `test_high_canonical_address_stack` | 48-bit canonical userspace stack top translation and validation | PASS | PASS |
| `test_error_handling_and_boundaries` | Zero SP, kernel SP rejection, and physical memory buffer exhaustion detection | PASS | PASS |

---

## 3. Verification Details

All tests compiled under `/W4 /WX /permissive-` with 0 warnings and executed with 0 failures in both Release and Debug configurations.
