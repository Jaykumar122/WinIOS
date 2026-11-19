# Stage 232.14 — Syscall Copyin/Copyout Memory Validation Architecture & Hardening

**Campaign:** 232  
**Stage:** 232.14  
**Component:** `winios::memory::GuestMemoryValidator`, `winios::memory::UserAddressSpace`, `winios::kernel::UserSyscallDispatcher`  
**Files Modified:**
- `src/memory/guest_memory_validator.h`
- `src/memory/guest_memory_validator.cpp`
- `src/memory/user_address_space.cpp`
- `src/kernel/user_syscall_dispatcher.cpp`

---

## 1. Threat Model & Audit Findings

During the Campaign 232 security audit (SEC-232-05), the syscall memory boundary was scrutinized for potential vulnerabilities when handling untrusted user pointers from guest applications:

1. **Unbounded Guest String Reads (SEC-232-05):**
   In `read_user_string`, if a guest passed a pointer to a sequence of non-zero bytes with no null terminator within the requested buffer length, the function read up to `max_len` bytes and returned `!out_str.empty()` (evaluating to `true`). This returned an unterminated string to host subsystem handlers (such as VFS file creation, directory operations, and property lookups).
2. **Missing `validate_guest_read` / `validate_guest_write` Abstractions:**
   The memory validator had generic `validate_buffer`, but lacked strongly-typed, self-documenting semantic range checkers for guest memory access modes.
3. **Implicit Null Pointer Validation in `UserAddressSpace`:**
   In `UserAddressSpace::validate_user_buffer`, `va == 0` was not explicitly checked upfront, relying solely on whether virtual page 0 was unmapped.
4. **String Null Termination Error Reporting:**
   `GuestMemoryValidator::copy_string` returned `ValidationResult::Success` even when the buffer was exhausted without discovering a null terminator byte.

---

## 2. Hardened Architecture & Implementation

### 2.1 Explicit Validation Results
Added `ValidationResult::StringNotTerminated` to `enum class ValidationResult`:
```cpp
enum class ValidationResult : std::uint32_t {
    Success = 0,
    NullPointer = 1,
    IntegerOverflow = 2,
    OutOfBounds = 3,
    UnmappedPage = 4,
    PermissionDenied = 5,
    MisalignedPointer = 6,
    CrossProcessViolation = 7,
    ZeroLengthForbidden = 8,
    StringNotTerminated = 9
};
```

### 2.2 Strict Copyin and Copyout Semantics
Introduced `validate_guest_read` and `validate_guest_write`:
- **`validate_guest_read(uas, va, size, alignment, allow_zero)`:**
  Asserts $va \ne 0$, $va + size \ge va$ (no overflow), $va + size \le kUserSpaceLimitVa$ (no EL1/kernel access), valid alignment, and that every 16 KB page across the range is mapped with EL0 read permissions.
- **`validate_guest_write(uas, va, size, alignment, allow_zero)`:**
  Performs all read checks plus verifies that every 16 KB page across the range has `fields.writable == true`. If any page in the span is read-only, it immediately returns `ValidationResult::PermissionDenied`.

### 2.3 Strict Null-Termination Defense (SEC-232-05 Fix)
Both `read_user_string` and `GuestMemoryValidator::copy_string` now enforce:
- `kMaxGuestPathLen = 1024`
- `kMaxGuestLogLen = 4096`
- Immediate rejection (`NullPointer` / `false`) on $va = 0$ or $va \ge kUserSpaceLimitVa$.
- Strict termination: if `\0` is not encountered within the bounded byte budget, execution returns `ValidationResult::StringNotTerminated` (or `false`), preventing any unterminated string from leaking across the kernel boundary into host services.
