# Stage 234.15: Guest Memory Range and Mapping Validation Audit

## Executive Summary
Stage 234.15 audits the memory safety, overflow prevention, page translation, permission enforcement, and string handling capabilities of the `GuestMemoryValidator` (`src/memory/guest_memory_validator.h` and `src/memory/guest_memory_validator.cpp`). This subsystem acts as the mandatory hardware/software validation boundary between untrusted ARM64 user-space pointers (EL0) and the hosting WinIOS runtime.

---

## 1. Audited Safety Invariants

| Validation Check | Audit Finding / Implementation | Diagnostic Code |
| :--- | :--- | :--- |
| **Null Pointer Guard** | Virtual address 0x0 is unconditionally trapped. | `ValidationResult::NullPointer` |
| **Host Pointer Guard** | Host source or destination pointer `nullptr` is trapped before copy. | `ValidationResult::NullPointer` |
| **Integer Overflow** | Tested against $2^{64}-1$ wrap (`va + size < va`). Prevents integer wrap around user space. | `ValidationResult::IntegerOverflow` |
| **Canonical User Limit** | Tested against `kUserSpaceLimitVa` (0x0000000800000000, 32 GiB A16 address space limit). | `ValidationResult::OutOfBounds` |
| **Page Table Walk** | Translates every virtual address to physical address via `UserAddressSpace::translate()`. | `ValidationResult::UnmappedPage` |
| **EL0 Accessibility** | Descriptors must have `fields.valid == true` and `fields.el0_accessible == true`. | `ValidationResult::UnmappedPage` |
| **Write Permission** | When `require_writable == true`, descriptor must have `fields.writable == true`. | `ValidationResult::PermissionDenied` |
| **Multi-Page Granules** | Traversal advances page-by-page (16 KB granules). Detects unmapped holes or permission drops mid-buffer. | `ValidationResult::UnmappedPage` / `PermissionDenied` |
| **Pointer Alignment** | Enforces `va % required_alignment == 0` for 2, 4, 8, 16-byte aligned structs. | `ValidationResult::MisalignedPointer` |
| **Zero-Length Policy** | Rejects 0-length buffers by default unless `allow_zero_length == true`. | `ValidationResult::ZeroLengthForbidden` |
| **Bounded String Copy** | Copies null-terminated C-strings up to `max_bytes`. Traps non-terminated strings and clears destination. | `ValidationResult::StringNotTerminated` |

---

## 2. Multi-Page Straddle & Permissions Analysis
A major class of emulator escape occurs when a guest process supplies a buffer that starts in a valid, mapped page but crosses into an unmapped or read-only page:
- `validate_buffer` calculates:
  ```cpp
  const std::uint64_t next_page = (cur & ~(kPhysicalPageSize16KB - 1ULL)) + kPhysicalPageSize16KB;
  cur = (next_page < end) ? next_page : end;
  ```
- It inspects each page's translation and attributes.
- If Page 1 is `RW` and Page 2 is `RO`, write validation fails immediately at Page 2, returning `PermissionDenied`.
- If Page 1 is mapped and Page 2 is unmapped, validation fails immediately at Page 2, returning `UnmappedPage`.
- No host memory reads or writes occur before the entire range has been validated.

---

## 3. Safe Copy Architecture (`copyin` / `copyout` / `copy_string`)
1. **Pre-Flight Range Check:** `copyin` and `copyout` validate the entire span prior to initiating the byte transfer.
2. **Chunked Transfers:** Copies proceed page-by-page according to physical page chunk sizes (`min(size - copied, 16KB - page_offset)`), preventing buffer overrun across disjoint physical pages.
3. **String Safety:** `copy_string` terminates on `\0`. If `max_bytes` is reached without a terminator, the output string is cleared and `StringNotTerminated` is returned, preventing unbounded heap reads.

---

## 4. Test Verification
The validation rules are tested across multiple test suites:
- `tests/unit/stage215_4_memory_validator_tests.cpp` (Foundational boundary tests)
- `tests/unit/stage233_15_syscall_adversarial_tests.cpp` (Adversarial matrix and alignment tests)
- `tests/unit/stage233_20_security_fuzzing_tests.cpp` (1,000 randomized boundary fuzz iterations)
All suites execute with 100% pass rates in both `RelWithDebInfo` and `Debug`.
