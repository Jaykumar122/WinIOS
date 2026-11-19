# Stage 233.14 — Guest Memory Validation Architecture & Security Audit

## 1. Scope & Objective

This audit independently reviews the memory safety mechanisms enforced at the guest-host syscall boundary in WinIOS. The guest runtime executes freestanding ARM64 Mach-O code in simulated EL0 userspace with a 16 KB page granule (matching the Apple A16 Bionic `T8120` profile on iPhone 15). The host syscall dispatcher runs in MSVC C++20 on Windows 11 x64.

Every guest pointer passed across the EL0 boundary must be strictly validated before dereferencing to prevent arbitrary host memory read/write, host code execution, or host denial of service.

The primary implementation under audit resides in:
- `src/memory/guest_memory_validator.h`
- `src/memory/guest_memory_validator.cpp`
- `src/memory/user_address_space.h`
- `src/kernel/user_syscall_dispatcher.cpp`

---

## 2. Architectural Analysis

### 2.1 Address Space Model & Limits
- **Virtual Address Space Limit:** `kUserSpaceLimitVa = 0x0000000800000000ULL` (32 GiB, corresponding to a 35-bit user address space).
- **Page Granule:** 16 KB (`kPhysicalPageSize16KB = 16384` bytes).
- **ARM64 Translation:** Multi-level translation using L2/L3 page tables, with page descriptor bits tracking `valid`, `el0_accessible`, `writable`, and `executable`.

### 2.2 Validation Primitives
The validator provides four core functions:
1. `validate_buffer(uas, va, size, require_writable, required_alignment, allow_zero_length)`:
   - Rejects `size == 0` unless `allow_zero_length` is explicitly set to `true`.
   - Rejects `va == 0` (NullPointer).
   - Enforces alignment check: `va % required_alignment == 0`.
   - Protects against 64-bit integer wraparound: `va + size < va`.
   - Enforces boundary: `va + size > kUserSpaceLimitVa`.
   - Iterates across all pages spanned by `[va, va + size)`:
     - Rejects unmapped or invalid translation (`pa == 0` or `!fields.valid`).
     - Rejects kernel/privileged pages (`!fields.el0_accessible`).
     - Enforces write permission if `require_writable == true` (`!fields.writable` yields `PermissionDenied`).
2. `copyin(uas, user_va, host_dst, size, required_alignment)`:
   - Validates `host_dst != nullptr`.
   - Executes `validate_buffer` across the full span before any byte is copied.
   - Performs page-chunked copying, verifying physical memory bounds (`pa + chunk <= uas.memory().size()`).
3. `copyout(uas, host_src, user_va, size, required_alignment)`:
   - Validates `host_src != nullptr`.
   - Executes `validate_buffer` with `require_writable == true` across the full span before writing.
   - Performs page-chunked copying into guest physical memory.
4. `copy_string(uas, user_va, max_bytes, out_str)`:
   - Rejects null and out-of-bounds start addresses.
   - Iterates up to `max_bytes`, inspecting each character in guest space.
   - Returns `StringNotTerminated` if `max_bytes` is exhausted without finding a null terminator (`\0`), preventing runaway host heap allocations.

---

## 3. Audit Findings & Threat Analysis

### Finding SEC-233-02: Double-Fetch (TOCTOU) Mitigation
- **Threat:** If the host reads guest memory multiple times (e.g., first reading a header to get a length, then reading the body), a concurrent guest thread or malicious emulator could alter the length between reads.
- **Analysis:** In WinIOS, all syscall data arguments are copied into a host-allocated buffer or stack structure using a single `copyin()` call before dispatcher logic inspects payload contents. Once in host memory, the guest cannot modify the parameters during execution.

### Finding SEC-233-03: Zero-Length Edge Cases
- **Threat:** Sycalls like `read(fd, buf, 0)` or `write(fd, buf, 0)` should return 0 without failing if `buf` is a valid pointer, but must not crash if `buf` is null or invalid.
- **Analysis:** `validate_buffer` defaults to `allow_zero_length = false` (`ZeroLengthForbidden`), requiring callers handling zero-length I/O to explicitly specify `allow_zero_length = true`. This prevents accidental bypass of pointer checks when `size == 0`.

### Finding SEC-233-04: Multi-Page Boundary Spanning
- **Threat:** A buffer starting in a valid mapped page may extend into an unmapped page or a read-only page.
- **Analysis:** The validator advances page by page:
  `const uint64_t next_page = (cur & ~(kPhysicalPageSize16KB - 1ULL)) + kPhysicalPageSize16KB;`
  and verifies every page in the range before allowing `copyin` or `copyout`. If any page fails, the entire transaction is rejected atomically without partial host writes.

---

## 4. Recommendations for Adversarial Test Suite (Stage 233.15)
To ensure comprehensive test coverage, Stage 233.15 must construct adversarial test vectors exercising:
1. Non-aligned pointers requiring 4-byte or 8-byte alignment.
2. 64-bit integer wraparound pointers (`0xFFFFFFFFFFFFFFF0 + 32`).
3. Buffers crossing from read-write pages into read-only or unmapped pages.
4. Strings lacking null terminators up to boundary limits.
5. Large buffer allocations exceeding guest memory bounds.
6. Zero-length buffer handling with `allow_zero_length` asserted and negated.
