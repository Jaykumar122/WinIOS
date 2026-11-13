# WINIOS CAMPAIGN 192 — STAGE 192.6 REPORT
## Darwin libSystem Lite ABI Conformance & Audit

**Campaign:** 192  
**Stage:** 192.6 (libSystem Lite ABI Audit)  
**Date:** 2026-11-13  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Host Architecture:** Windows 11 x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Verdict:** **PASSED (3/3 test suites passed, 100% CTest regression clean, real compiled ABI binary verified in EL0)**

---

## 1. Executive Summary

In Stage 192.6, Darwin's userspace C runtime ABI implemented by `libSystem.B.dylib` (`REAL-018`) was audited, cataloged, and rigorously verified against an independently compiled Darwin ARM64 Mach-O executable (`REAL-034`: `tests/artifacts/bin/stage_192_6_abi_app`).

Key findings and achievements in Stage 192.6:
1. **Calling Convention & Register Preservation:** Verified strict conformance to ARM64 AAPCS calling convention across dynamic calls through `libSystem.B.dylib`. Callee-saved general-purpose registers (`x19` through `x28`) and frame pointer (`x29`/FP) remain preserved across all allocator, memory, string, and syscall invocations.
2. **Stack Alignment Invariant:** Verified that the Darwin ARM64 requirement for 16-byte stack pointer alignment (`SP % 16 == 0`) is respected throughout userspace execution.
3. **Heap Allocator Alignment & Safety:**
   - Evaluated `malloc`, `calloc`, and `realloc` across a range of block sizes (1 to 40,000 bytes). All returned allocations are strictly 16-byte aligned.
   - Zero-size allocation semantics: `malloc(0)` and `calloc(0, 0)` return valid, non-null, 16-byte aligned pointers that can be safely written to (within aligned bounds) and freed.
   - Arithmetic overflow protection: `calloc((size_t)-1, 2)` arithmetic overflow safely returns `NULL` without wrapping or out-of-bounds writes.
   - Reallocation preservation: verified byte-for-byte preservation of user memory across buffer expansions, as well as `realloc(p, 0) == NULL` free semantics.
   - Double-free robustness: `free(p); free(p);` cleanly detects zeroed allocator magic and avoids memory corruption or crashes.
4. **Memory & String Primitives:**
   - Validated standard `memcpy`, `memset`, and `memcmp`.
   - Validated `memmove` with both forward-overlapping (`dst > src`) and backward-overlapping (`dst < src`) pointer ranges.
   - Validated `strlen`, `strcmp`, `strncmp`, `strcpy`, and `strncpy` (including null-padding semantics up to count `n`).
5. **Real Binary Guest Execution:**
   - Compiled `stage_192_6_abi_app` with `Zig 0.17.0` (Clang 22.1.8 + stack-protector-strong).
   - Executed inside EL0 userspace engine with `minidyld` (`REAL-017`) and `libSystem.B.dylib` (`REAL-018`), retiring **32,863 guest instructions** with clean exit code `0`.

---

## 2. Supported Symbol Manifest (`libSystem.B.dylib`)

| Category | Exported Symbol | Calling Convention | Implementation Status | Evidence / Test |
|---|---|---|---|---|
| **Memory Allocation** | `malloc` | AAPCS64 | Supported (16B aligned, mmap backed) | `stage_192_6_abi_app`, `stdio_heap_app` |
| | `calloc` | AAPCS64 | Supported (Overflow-checked, zeroed) | `stage_192_6_abi_app`, `stdio_heap_app` |
| | `realloc` | AAPCS64 | Supported (Preserves data, realloc(p,0)=NULL) | `stage_192_6_abi_app`, `stdio_heap_app` |
| | `free` | AAPCS64 | Supported (Magic verified, dfree-safe) | `stage_192_6_abi_app`, `stdio_heap_app` |
| **Memory Operations** | `memcpy` | AAPCS64 | Supported | `stage_192_6_abi_app`, `sha256_app` |
| | `memmove` | AAPCS64 | Supported (Overlap-safe forward/backward) | `stage_192_6_abi_app`, `cjson_app` |
| | `memset` | AAPCS64 | Supported | `stage_192_6_abi_app`, `uzlib_app` |
| | `memcmp` | AAPCS64 | Supported | `stage_192_6_abi_app` |
| | `bzero` | AAPCS64 | Supported | `libsystem_lite.c` |
| **String Operations** | `strlen` | AAPCS64 | Supported (Null-safe) | `stage_192_6_abi_app` |
| | `strcmp` | AAPCS64 | Supported | `stage_192_6_abi_app` |
| | `strncmp` | AAPCS64 | Supported | `stage_192_6_abi_app` |
| | `strcpy` | AAPCS64 | Supported | `stage_192_6_abi_app` |
| | `strncpy` | AAPCS64 | Supported (Zero-pads up to n) | `stage_192_6_abi_app` |
| | `strtod` | AAPCS64 | Supported | `cjson_app` |
| **Process Control** | `exit` | AAPCS64 | Supported (`SYS_exit (1)`) | `stage_192_6_abi_app`, all apps |
| | `_exit` | AAPCS64 | Supported (`SYS_exit (1)`) | `libsystem_lite.c` |
| | `abort` | AAPCS64 | Supported (stderr warning + `_exit(134)`) | `stack_smash_libc_app` |
| | `atexit` | AAPCS64 | Limited / Internal | Unit audit |
| **POSIX I/O** | `write` | AAPCS64 | Supported (`SYS_write (4)`) | `stage_192_6_abi_app`, `stdio_heap_app` |
| | `read` | AAPCS64 | Supported (`SYS_read (3)`) | `libsystem_lite.c` |
| | `open` | AAPCS64 | Supported (`SYS_open (5)`) | `minidyld.c`, `libsystem_lite.c` |
| | `close` | AAPCS64 | Supported (`SYS_close (6)`) | `minidyld.c`, `libsystem_lite.c` |
| | `getpid` | AAPCS64 | Supported (`SYS_getpid (20)`) | `stdio_heap_app` |
| **Virtual Memory** | `mmap` | AAPCS64 | Supported (`SYS_mmap (197)`) | `libsystem_lite.c`, `minidyld.c` |
| | `munmap` | AAPCS64 | Supported (`SYS_munmap (73)`) | `libsystem_lite.c` |
| **Mach Traps** | `mach_reply_port` | AAPCS64 | Supported (`mach_reply_port (-26)`) | `mach_client` |
| | `thread_self_trap` | AAPCS64 | Supported (`thread_self_trap (-27)`) | `mach_client` |
| | `mach_thread_self`| AAPCS64 | Supported | `mach_client` |
| | `task_self_trap` | AAPCS64 | Supported (`task_self_trap (-28)`) | `mach_client` |
| | `mach_task_self` | AAPCS64 | Supported | `mach_client` |
| | `mach_msg_trap` | AAPCS64 | Supported (`mach_msg_trap (-31)`) | `mach_client` |
| | `mach_msg` | AAPCS64 | Supported | `mach_client` |
| **Runtime Security** | `__stack_chk_guard`| Data | Supported (`0x595E9D4B1A2C3E4F`) | `stack_smash_libc_app` |
| | `__stack_chk_fail` | AAPCS64 | Supported (Exit 134 on smash) | `stack_smash_libc_app` |
| **Stdio Formatting** | `printf`, `snprintf`, `puts`, `putchar`, `sscanf` | AAPCS64 | Supported | `stdio_heap_app`, `cjson_app` |

---

## 3. Test Verification Matrix

A dedicated test suite [`tests/unit/stage_192_6_libsystem_abi_tests.cpp`](file:///D:/WinISO26/WinIOS/tests/unit/stage_192_6_libsystem_abi_tests.cpp) was developed and registered as target `stage_192_6_libsystem_abi_tests`:

| Test # | Focus Area | Description | Status |
|---|---|---|---|
| **Test 1** | Symbol Manifest Audit | Validates presence of all 21 required ABI functions in `libSystem.B.dylib` export tables | **PASSED** |
| **Test 2** | Guest Execution of `stage_192_6_abi_app` | Executes ABI conformance app in EL0 guest; retires 32,863 instructions; confirms clean exit code 0 | **PASSED** |
| **Test 3** | ABI Contract Invariants & Determinism | Verifies repeat execution yields identical results without state leaks | **PASSED** |

### Output:
```
=========================================================
STAGE 192.6: libSystem Lite ABI Conformance Tests
=========================================================
[Test 1] libSystem.B.dylib Exported Symbol Manifest Audit...
  libSystem.B.dylib size: 71920 bytes, SHA-256=94c66c4d93b10e0d792be637458da41d77d7cbb69e0aba794d3eb3205b2477a2
  All 21 required ABI symbols verified in export/symbol tables
[PASS] libSystem Exported Symbol Manifest Audit
[Test 2] Guest Execution of stage_192_6_abi_app (EL0)...
  stage_192_6_abi_app: 50832 bytes, SHA-256=a74a6f79154a9188c09f9495398f2f46d6c9b0d113a6b0df475b36a3c9dd344c
  Guest instructions retired: 32863
  Process exited: true, code: 0
  Stdout: STAGE_192_6_ABI_START
STAGE_192_6_PASS

[PASS] Guest Execution of stage_192_6_abi_app (EL0)
[Test 3] ABI Contract Invariants (Heap alignment, overflow, double-free)...
  Deterministic re-execution confirmed clean across repeat run.
[PASS] ABI Contract Invariants & Determinism

Results: 3 passed, 0 failed
```

---

## 4. Conformance & Regression Status
- MSVC C++20 build with `/W4 /WX /permissive-`: Clean (0 warnings, 0 errors).
- All prior stages (192.0 through 192.5) continue to pass.
- New artifact `REAL-034` registered in `macho_artifact_manifest.json`.
