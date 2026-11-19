# Stage 233.15 — Syscall Boundary Adversarial Test Results

## Executive Summary

Stage 233.15 implemented and executed the adversarial test suite `stage233_15_syscall_adversarial_tests`, systematically subjecting the guest-to-host syscall boundary and memory validator to malicious, boundary-violating, and ill-formed inputs. In addition, during audit of `copy_string`, a security hardening was introduced to ensure that upon any error (including reaching maximum search limit or encountering unmapped memory), the destination string buffer is cleared immediately rather than exposing partial un-terminated substrings.

All test suites compiled cleanly under MSVC C++20 `/W4 /WX /permissive-` and passed 100% in both `RelWithDebInfo` and `Debug` configurations.

---

## Adversarial Test Suites & Results

| Test Category | Injected Threat / Condition | Expected Behavior | Actual Result |
| :--- | :--- | :--- | :--- |
| **Pointer Alignment** | Misaligned virtual addresses (+1, +2, +3, +7) for 4-byte and 8-byte structures. | Rejection with `MisalignedPointer`. | **PASS** — Alignment strictly enforced on `validate_buffer`, `copyin`, and `copyout`. |
| **Integer Overflow & Space Limits** | `UINT64_MAX`, `0xFFFFFFFFFFFFFFF0 + 20`, and addresses >= `kUserSpaceLimitVa` (32 GiB). | Rejection with `IntegerOverflow` or `OutOfBounds`. | **PASS** — 64-bit wraparounds and EL1/kernel limit breaches rejected unconditionally. |
| **Cross-Page Spanning & Permissions** | 32-byte buffer spanning mapped read-write page into unmapped page; buffer spanning read-write into read-only page. | `UnmappedPage` on unmapped second page; `PermissionDenied` on write to read-only second page. | **PASS** — Atomic verification across 16 KB page boundaries before performing copies. |
| **String Bounds & Non-Termination** | 16 KB page filled with non-null bytes ('A'); strings crossing into unmapped memory. | Rejection with `StringNotTerminated` / `UnmappedPage`; zero buffer leakage. | **PASS** — Output string cleared on failure; maximum search limit strictly respected. |
| **Zero-Length Buffer Policies** | Size 0 buffer with default policy vs `allow_zero_length = true`. | `ZeroLengthForbidden` by default; `Success` only when explicitly permitted. | **PASS** — Prevents silent bypass of buffer validation on zero length. |
| **Direct Syscall Dispatcher Invocations** | Syscalls 581, 582, 586, 592 with null, unmapped, overflow, read-only, and un-terminated inputs. | Dispatcher returns non-zero POSIX error codes (`EINVAL`, `EFAULT`, `EPERM`); zero host crashes. | **PASS** — All invalid syscall requests safely caught and logged. |

---

## Hardening Implemented: SEC-233-05 (String Sanitization on Error)
In `src/memory/guest_memory_validator.cpp`:
- `GuestMemoryValidator::copy_string` was updated so that if a string fails validation (whether by reaching `max_bytes` without a null terminator, or encountering an unmapped/out-of-bounds page), `out_str.clear()` is called before returning `StringNotTerminated`, `OutOfBounds`, or `UnmappedPage`.
- This ensures that consuming syscall handlers can never read partially copied un-terminated strings if the return status is mishandled.
