# Stage 234.16: Syscall Boundary Matrix and Table-Driven Verification

## Executive Summary
Stage 234.16 implements comprehensive, table-driven security verification across every pointer-taking system call implemented in the WinIOS runtime (`UserSyscallDispatcher` in `src/kernel/user_syscall_dispatcher.cpp`). This matrix ensures that adversarial guest user-space pointers (EL0) cannot induce segmentation faults, memory corruption, unhandled exceptions, or arbitrary host memory reads/writes.

---

## 1. Pointer-Taking Syscall Matrix & Tested Conditions

Every pointer-taking syscall was tested against 4–5 adversarial memory configurations:
1. **Null Pointer (0x0):** Trapped prior to translation.
2. **Integer Overflow Pointer (`0xFFFFFFFFFFFFFFF0`):** Wraparound boundary prevented.
3. **Unmapped Page Pointer (`0x80000`):** Unmapped translation returns fault.
4. **Read-Only Page Violation (`0x40000`):** Buffer write operations to read-only pages rejected.
5. **Unterminated C-String:** Non-terminated strings bounded by buffer lengths.

| Syscall # | Syscall Name | Tested Register | Type | Write Required? | Null Guard | Overflow Guard | Unmapped Guard | RO / Unterminated Guard | Result |
| :---: | :--- | :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **3** | `read` | X1 | Buffer | Yes | PASS | PASS | PASS | PASS (RO) | **SECURE** |
| **4** | `write` | X1 | Buffer | No | PASS | PASS | PASS | N/A | **SECURE** |
| **5** | `open` | X0 | C-String | No | PASS | PASS | PASS | PASS (Unterm) | **SECURE** |
| **339** | `stat64` | X0 | C-String | No | PASS | PASS | PASS | PASS (Unterm) | **SECURE** |
| **338** | `fstat64` | X1 | Struct Buffer | Yes | PASS | PASS | PASS | PASS (RO) | **SECURE** |
| **506** | `set_window_title` | X1 | C-String | No | PASS | PASS | PASS | PASS (Unterm) | **SECURE** |
| **516** | `storage_read` | X0 | C-String | No | PASS | PASS | PASS | PASS (Unterm) | **SECURE** |
| **517** | `storage_write` | X0 | C-String | No | PASS | PASS | PASS | PASS (Unterm) | **SECURE** |
| **519** | `ipc_send` | X1 | Buffer | No | PASS | PASS | PASS | N/A | **SECURE** |
| **520** | `ipc_recv` | X1 | Buffer | Yes | PASS | PASS | PASS | PASS (RO) | **SECURE** |
| **534** | `clipboard_get` | X0 | Buffer | Yes | PASS | PASS | PASS | PASS (RO) | **SECURE** |
| **535** | `clipboard_set` | X0 | Buffer | No | PASS | PASS | PASS | N/A | **SECURE** |
| **581** | `settings_get` | X0 | Struct Buffer | Yes | PASS | PASS | PASS | PASS (RO) | **SECURE** |
| **586** | `task_enumerate` | X0 | Struct Buffer | Yes | PASS | PASS | PASS | PASS (RO) | **SECURE** |
| **589** | `accessibility_get` | X0 | Struct Buffer | Yes | PASS | PASS | PASS | PASS (RO) | **SECURE** |
| **591** | `diag_event_query` | X0 | Struct Buffer | No | PASS | PASS | PASS | N/A | **SECURE** |
| **592** | `app_info` | X0 | C-String | No | PASS | PASS | PASS | PASS (Unterm) | **SECURE** |
| **593** | `app_list` | X0 | Struct Buffer | Yes | PASS | PASS | PASS | PASS (RO) | **SECURE** |
| **575** | `doc_stream_read` | X1 | Buffer | Yes | PASS | PASS | PASS | PASS (RO) | **SECURE** |
| **576** | `doc_stream_write` | X1 | Buffer | No | PASS | PASS | PASS | N/A | **SECURE** |

---

## 2. Invariants & Error Reporting
- In every failure mode, the dispatcher intercepted the invalid pointer within `GuestMemoryValidator` / `UserAddressSpace` translation prior to host buffer dereferencing.
- The dispatcher cleanly set the error status in register X0 (`-1` or error code) and set the thread `errno` (e.g. `EFAULT`, `EINVAL`, or `EPERM`).
- Zero host crashes, access violations, or unhandled exceptions occurred across all 20 pointer-taking syscalls and all adversarial input variations.

---

## 3. Test Verification
The dedicated suite `tests/unit/stage234_16_syscall_matrix_tests.cpp` was compiled with MSVC C++20 (`/W4 /WX /permissive-`) and executed:
- `RelWithDebInfo`: Passed 100%
- `Debug`: Passed 100%
