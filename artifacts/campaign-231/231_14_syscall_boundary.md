# Stage 231.14 — ABI Boundary and Memory Safety Tests

## Overview
Stage 231.14 validates the host kernel memory safety and adversarial boundary defenses across Category C ABI v2.4 (Syscalls 581 through 593). It ensures that all guest-supplied pointers, unmapped memory addresses, out-of-range parameters, invalid enums, and non-existent entity IDs return proper error codes (`EFAULT`, `EINVAL`, `ENOENT`, `ESRCH`, `EPERM`) without causing host crashes, memory corruption, or undefined behavior.

## Key Boundaries Evaluated
1. **Syscalls 581..585 (Settings, Theme & Control Center)**:
   - `SYS_WINIOS_SETTINGS_GET` (581): Verified null pointer and unmapped guest memory page rejections.
   - `SYS_WINIOS_SETTINGS_SET` (582): Verified null pointer rejection and capability requirements.
   - `SYS_WINIOS_THEME_GET` (583): Verified copyin safety on unmapped target addresses.
   - `SYS_WINIOS_THEME_SET` (584): Verified rejection of out-of-bounds theme mode (> 2) and accent color (> 7) with `EINVAL`.
2. **Syscalls 586..588 (Multitasking & Lifecycle)**:
   - `SYS_WINIOS_TASK_ENUMERATE` (586): Verified null buffer address and unmapped destination page rejections.
   - `SYS_WINIOS_TASK_SWITCH` (587): Verified rejection of non-existent PID (999999) with `ESRCH`.
   - `SYS_WINIOS_TASK_TERMINATE` (588): Verified rejection of invalid PID with `ESRCH`.
3. **Syscalls 591..593 (Diagnostics & Application Discovery)**:
   - `SYS_WINIOS_DIAG_EVENT_QUERY` (591): Verified null request pointer and unmapped `buffer_va` payload destination rejections.
   - `SYS_WINIOS_APP_INFO` (592): Verified null bundle ID, unmapped bundle ID, and query for non-existent applications (`ENOENT`).
   - `SYS_WINIOS_APP_LIST` (593): Verified null request pointer and unmapped `buffer_va` target address rejections.
4. **Privilege & Capability Enforcement**:
   - Verified that unprivileged processes lacking `SettingsWrite` cannot mutate settings or themes (`EPERM`).
   - Verified that unprivileged processes lacking `TaskManage` cannot terminate external processes (`EPERM`).

## Verification
- **Test Suite**: `tests/unit/stage231_14_syscall_boundary_tests.cpp` (CTest #916).
- **Test Results**:
  - RelWithDebInfo: Test #916 Passed in 0.06s.
  - Debug: Test #916 Passed in 0.11s.
