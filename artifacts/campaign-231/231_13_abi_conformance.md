# Stage 231.13 — Category C ABI v2.4 Conformance Audit

## Overview
Stage 231.13 conducts a comprehensive static and runtime ABI conformance audit of Category C ABI v2.4 (Syscalls 581 through 593). It validates bit-for-bit equivalence in structure sizes, field offsets, member types, alignments, and syscall dispatch numbering between the guest SDK (`guest-sdk/include/winios/winios_types.h`) and the host kernel dispatcher (`src/kernel/user_syscall_dispatcher.h`).

## Category C Syscall Directory (581..593)

| Syscall # | Dispatch Constant | Name | Arguments | Return | Associated Struct & Size |
|---|---|---|---|---|---|
| **581** | `kSysWiniosSettingsGet` | `SYS_WINIOS_SETTINGS_GET` | `arg0: out_ptr` | 0 on success, -EFAULT | `winios_settings_snapshot_t` (48 B) |
| **582** | `kSysWiniosSettingsSet` | `SYS_WINIOS_SETTINGS_SET` | `arg0: in_ptr` | 0 on success, -EFAULT | `winios_settings_snapshot_t` (48 B) |
| **583** | `kSysWiniosThemeGet` | `SYS_WINIOS_THEME_GET` | `arg0: out_mode_ptr`, `arg1: out_accent_ptr` | 0 on success, -EFAULT | Raw uint32_t values |
| **584** | `kSysWiniosThemeSet` | `SYS_WINIOS_THEME_SET` | `arg0: mode`, `arg1: accent` | 0 on success, -EINVAL | Raw uint32_t values |
| **585** | `kSysWiniosControlCenterToggle` | `SYS_WINIOS_CONTROL_CENTER_TOGGLE` | None | 0 on success, -ENODEV | None |
| **586** | `kSysWiniosTaskEnumerate` | `SYS_WINIOS_TASK_ENUMERATE` | `arg0: out_buf_ptr`, `arg1: max_entries` | Count on success, -EFAULT | `winios_task_entry_t` (120 B) |
| **587** | `kSysWiniosTaskSwitch` | `SYS_WINIOS_TASK_SWITCH` | `arg0: target_pid` | 0 on success, -ESRCH | None |
| **588** | `kSysWiniosTaskTerminate` | `SYS_WINIOS_TASK_TERMINATE` | `arg0: target_pid`, `arg1: graceful` | 0 on success, -ESRCH | None |
| **589** | `kSysWiniosAccessibilityGet` | `SYS_WINIOS_ACCESSIBILITY_GET` | `arg0: out_ptr` | 0 on success, -EFAULT | `winios_accessibility_info_t` (32 B) |
| **590** | `kSysWiniosAccessibilitySet` | `SYS_WINIOS_ACCESSIBILITY_SET` | `arg0: in_ptr` | 0 on success, -EFAULT | `winios_accessibility_info_t` (32 B) |
| **591** | `kSysWiniosDiagEventQuery` | `SYS_WINIOS_DIAG_EVENT_QUERY` | `arg0: req_ptr` | Count on success, -EFAULT | `winios_diag_query_req_t` (32 B) / `winios_diag_event_record_t` (48 B) |
| **592** | `kSysWiniosAppInfo` | `SYS_WINIOS_APP_INFO` | `arg0: bundle_id_str`, `arg1: out_info_ptr` | 0 on success, -ENOENT/-EFAULT | `winios_app_info_t` (128 B) |
| **593** | `kSysWiniosAppList` | `SYS_WINIOS_APP_LIST` | `arg0: req_ptr` | Count on success, -EFAULT | `winios_app_list_req_t` (24 B) |

## Layout Verification Matrix
- `sizeof(winios_settings_snapshot_t)` = 48 bytes (12 fields x 4 bytes, pack(8)).
- `sizeof(winios_task_entry_t)` = 120 bytes (`pid`, `state`, `is_foreground`, `pad`, `primary_window`, `name[32]`, `bundle_id[64]`).
- `sizeof(winios_accessibility_info_t)` = 32 bytes (5 uint32_t fields + 3 uint32_t pad).
- `sizeof(winios_diag_event_record_t)` = 48 bytes (2 uint64_t + 3 uint32_t + 1 int32_t + 16-byte tag).
- `sizeof(winios_diag_query_req_t)` = 32 bytes (1 uint64_t + 4 uint32_t + 1 uint64_t buffer_va).
- `sizeof(winios_app_info_t)` = 128 bytes (`bundle_id[64]`, `display_name[32]`, `version[16]`, 2 uint32_t, 1 uint64_t quota).
- `sizeof(winios_app_list_req_t)` = 24 bytes (4 uint32_t + 1 uint64_t buffer_va).

## Verification
- **Test Suite**: `tests/unit/stage231_13_abi_conformance_tests.cpp` (CTest #915).
- **Test Results**:
  - RelWithDebInfo: Test #915 Passed in 0.06s.
  - Debug: Test #915 Passed in 0.05s.
