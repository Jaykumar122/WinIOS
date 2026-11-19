# Stage 231.9 — Process Identity and Handle Security Hardening

## Overview
Stage 231.9 hardens process identity, handle validation, and slot recycling within `winios::kernel::ProcessIdentityManager`. It eliminates process ID / handle spoofing and generational wrapping vulnerabilities (SEC-231-03).

## Key Implementation Changes
1. **Generational Overflow and Slot Retirement (`kMaxGeneration = 0x7FFFFFFFu`)**:
   - Monotonically increments slot generation upon termination.
   - When a slot's generation reaches `kMaxGeneration`, the slot is permanently retired into `retired_pids_` and is never re-allocated or wrapped to 1.
   - `is_retired_pid(uint32_t pid)` allows verification of permanently exhausted slot identifiers.
2. **CSPRNG 128-bit Process Security Token (`process_token`)**:
   - Generated at process instance creation using Windows CNG (`BCryptGenRandom`) via `winios::security::SecureRandom::get_random_hex(16)` (32 hex characters / 128 bits of cryptographic entropy).
   - Unpredictable across process instances, preventing cross-generational handle forging.
3. **Handle and Token Validation (`validate_handle_and_token`)**:
   - `validate_handle_and_token(ProcessHandle handle, const std::string& token)` validates both the 64-bit handle (PID + generation match against active slot) and constant-time match of the security token using `winios::security::constant_time_equals`.
4. **Thread-Safety**:
   - All allocation, retirement, recycling, and lookup operations guarded under `std::mutex state_mutex_`.
   - PID allocation avoids infinite loops when slots are exhausted or retired.

## Verification
- **Test Suite**: `tests/unit/stage231_9_process_identity_hardening_tests.cpp` (CTest #911).
- **Test Scenarios**:
  1. *Generation Increment and Handle Invalidation*: Verifies that terminating a process advances the slot generation and invalidates old handles (`is_valid_handle` returns `false`).
  2. *Generational Retirement (No Wrap)*: Simulates slot reaching `kMaxGeneration`, confirms slot is retired and marked by `is_retired_pid`, and cannot wrap or be re-allocated.
  3. *CSPRNG Process Token Uniqueness*: Generates multiple process instances, verifying each receives a distinct 32-character hex token with high entropy.
  4. *Token-Bound Handle Validation*: Validates that `validate_handle_and_token` strictly checks PID, generation, and token, rejecting mismatched tokens or dead handles.
  5. *Thread-Safe Concurrent Allocations*: Validates multi-threaded process creation and termination without corruption or duplicate active PIDs.
- **Results**:
  - RelWithDebInfo: Test #911 Passed in 0.06s.
  - Debug: Test #911 Passed in 0.09s.
