# WINIOS CAMPAIGN 220 — STAGE 220.13 SYSCALL DISPATCHER HARDENING & AUDIT REPORT

## 1. Executive Summary

This audit assesses the input validation, memory safety, and boundary defense mechanisms implemented in `UserSyscallDispatcher` (`src/kernel/user_syscall_dispatcher.cpp`). System calls represent the primary boundary between unprivileged ARM64 EL0 guest code and the host-simulated kernel execution environment.

---

## 2. Audited Vulnerability Vectors & Remediations

### 2.1 Integer Overflow and Wrap-Around Vulnerabilities
- **Vector:** An adversary passes a large virtual address `buf_va = 0xFFFFFFFFFFFFFF00ULL` with `nbyte = 0x200ULL` to `SYS_read` or `SYS_write`. In standard 64-bit arithmetic, `buf_va + nbyte` wraps around to `0x100ULL`, bypassing naive range checks (`buf_va + nbyte <= upper_limit`).
- **Remediation:** Added explicit wrap-around checks:
  ```cpp
  if (buf_va + nbyte < buf_va || buf_va + nbyte >= 0x0001000000000000ULL || nbyte > 0x10000000ULL) {
      ++stats_.total_invalid_pointers;
      return record_error(kErrnoEINVAL);
  }
  ```
- **Result:** Any wrapping buffer pointer is deterministically caught and rejected with `EINVAL` (22).

### 2.2 Memory Exhaustion via Oversized Allocations
- **Vector:** An attacker calls `SYS_read` or `SYS_write` with `nbyte = 0xFFFFFFFFFFFFFFFFULL` or `nbyte = 2 GB`, causing host heap exhaustion or integer overflow when creating temporary intermediate transfer buffers.
- **Remediation:** Enforced an architectural upper bound of 256 MiB (`0x10000000ULL`) per individual transfer request.
- **Result:** Memory allocation crashes (`std::bad_alloc`) are eliminated.

### 2.3 Pointer Alignment & Unmapped Memory Access
- **Vector:** Guest code passes misaligned pointers to typed structures (e.g. `SYS_pipe` with `fds_va = 0x20001ULL` or `SYS_mutex_lock` with `mutex_va = 0x10001ULL`).
- **Remediation:** Enforced `(va & 0x3) == 0` for 32-bit integer structures and `uas.validate_user_buffer()` across the entire buffer range before kernel dereferencing.
- **Result:** Memory access violations fail safely and return `EFAULT` (14).

### 2.4 File Descriptor Index Out-of-Bounds
- **Vector:** Passing negative integer descriptors or descriptors `>= 64`.
- **Remediation:** Every descriptor access verifies `proc.fd_table().is_valid_fd(fd)` prior to dereferencing table slots.
- **Result:** Negative or out-of-range descriptors consistently return `EBADF` (9).

### 2.5 Access Mode Violation
- **Vector:** Calling `SYS_write` on read-only descriptors (such as `stdin`, fd 0) or `SYS_read` on write-only descriptors.
- **Remediation:** Mandatory verification of `desc_entry->writable` for writes and `desc_entry->readable` for reads.
- **Result:** Mode violations consistently return `EBADF` (9).

---

## 3. Conformance Summary

| Test Case | Attack / Fault Input | Expected Result | Verified Result |
| :--- | :--- | :--- | :--- |
| Integer Wrap | `buf = ~0ULL - 0x100, n = 0x200` | `kErrnoEINVAL` (22) | PASS |
| Buffer Cap | `nbyte = 0xFFFFFFFFFFFFFFFFULL` | `kErrnoEINVAL` (22) | PASS |
| Pipe Misalign | `fds_va = 0x20001ULL` (unaligned) | `kErrnoEFAULT` (14) | PASS |
| Negative FD | `fd = -1` on `SYS_close` | `kErrnoEBADF` (9) | PASS |
| Read-only Write | `write(0, buf, 10)` | `kErrnoEBADF` (9) | PASS |
| Unknown Syscall | `syscall 9999` | `kErrnoENOSYS` (78) | PASS |
