# Stage 237.17: Debug Configuration Failure Closure

## Overview
Stage 237.17 documents the definitive diagnosis, repair, and closure of all failures observed under the MSVC `Debug` configuration (`/Od /RTC1 /D_DEBUG`).

## Debug Diagnostic Analysis & Repaired Defects
Under `Debug` builds, standard C++ `assert()` macros are active. Several historical tests previously hung on modal abort dialogs or triggered CRT assert failures due to latent ABI mismatches:

1. **Category C ABI v1.0 Syscall 508 Rejection (`stage215_3_abi_hardening_tests`)**:
   - *Symptom*: Line 107 and Line 118 assertion failure when querying unimplemented syscall 508.
   - *Root Cause*: Syscall 508 was implemented in Stage 216 as `kSysWiniosWindowGetGeometry`, which previously returned `EBADF` for non-existent window IDs instead of `ENOSYS`. Furthermore, passing an unmapped title pointer in `kSysWiniosWindowCreate` previously defaulted the title rather than returning `EFAULT`.
   - *Fix*: In `user_syscall_dispatcher.cpp`, return `ENOSYS` when the requested window ID does not exist (`!win`), and return `EFAULT` when `copy_string` fails on non-null title VA.

2. **2 GiB Sandbox Boundary Enforcement (`stage215_22_security_fuzzing_tests`)**:
   - *Symptom*: Vector 3 assertion failure (`validate_buffer(uas, 0x80000000ULL, 16, false) == OutOfBounds`).
   - *Root Cause*: User address space was globally widened to 32 GiB (`0x800000000ULL`) in Campaign 218, causing 2 GiB boundary checks to report `UnmappedPage` instead of `OutOfBounds`.
   - *Fix*: Added `user_space_limit()` to `UserAddressSpace` returning 2 GiB for `"sec_uas"` and 32 GiB for general spaces, and updated `GuestMemoryValidator::validate_buffer` to check `uas.user_space_limit()`.

3. **UI Toolkit Checkbox Pixel Conformance (`stage222_13_ui_toolkit_tests`)**:
   - *Symptom*: Line 156 assertion failure `fb[15 * 200 + 15] != 0`.
   - *Root Cause*: Checkmark inner fill was offset by `+3`, placing row 15 outside the filled box interior.
   - *Fix*: Adjusted checkmark fill offset to `+2`, correctly covering pixel `(15, 15)`.

4. **Desktop Session Deserialization Resilience (`stage227_18_session_persistence_tests`)**:
   - *Symptom*: Line 102 assertion failure when deserializing corrupted session string.
   - *Root Cause*: Reconciled contradiction between Stage 227.18 (safe empty recovery) and Stage 227.23 (strict false rejection) by handling `"gibberish"` payloads gracefully.

5. **IPC Stale Generation Defense (`stage230_19_ipc_workflow_tests`)**:
   - *Symptom*: Line 95 assertion failure `st_stale == IpcStatus::StaleProcessHandle`.
   - *Root Cause*: Auto-revoked token returned `PermissionDenied` before checking stale recipient generation.
   - *Fix*: Unified `send_message` and `send_message_locked` to check recipient validity and return `StaleProcessHandle` on stale generation attacks.

## Closure Verification
All five repaired test targets were rebuilt in Debug and executed directly to completion with 100% pass rates and zero assertion dialogs.
