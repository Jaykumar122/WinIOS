# Stage 237.5 — Legacy UI and Windowing Repairs

## 1. Executive Summary

During Campaign 237, an exhaustive audit of legacy native window applications (Campaigns 222–225) was conducted. Ten test suites previously failed with assertions such as `wm.window_count() == 1` or storage readback failures:
- `stage222_20_interactive_notes_tests`
- `stage222_21_runtime_monitor_tests`
- `stage223_19_calculator_tests`
- `stage223_20_notes_v2_tests`
- `stage223_21_resource_browser_tests`
- `stage224_22_platform_settings_tests`
- `stage224_22_ipc_messenger_tests`
- `stage225_21_device_center_tests`
- `stage225_21_system_monitor_tests`
- `stage225_22_multi_app_device_tests`

---

## 2. Root Cause Discovery: String Boundary Null-Termination Defect

In `src/kernel/user_syscall_dispatcher.cpp`, userspace string parsing functions (`copy_string` and `read_user_string`) enforce strict null-termination security checks (SEC-232-05).
However, guest callers in `guest-sdk/lib/winios_runtime.c` pass `strlen(str)` as `arg1` (e.g. `title_len` or `path_len` or `key_len`), which represents the count of non-null bytes.

### The Defect:
When `copy_string` or `read_user_string` bounded its iteration to `max_len = strlen(str)`, the loop examined only the first `strlen(str)` characters. Because the null-terminator `\0` resides at offset `strlen(str)` (the $(strlen+1)$-th byte), the loop finished without encountering `\0` and returned `ValidationResult::StringNotTerminated` / `EFAULT`.

As a direct result:
1. `winios_window_create("Interactive Notes", ...)` failed with `EFAULT`.
2. The guest application skipped all UI initialization, never created a window, and exited without writing storage.
3. Tests subsequently failed asserting `wm.window_count() == 1` or storage file existence.

---

## 3. Implementation Corrections

In `src/kernel/user_syscall_dispatcher.cpp`:
1. **Window Creation Syscall 500 (`kSysWiniosWindowCreate`):**
   ```cpp
   std::size_t max_len = (arg1 > 0) ? std::min<std::size_t>(static_cast<std::size_t>(arg1) + 1, 256u) : 256u;
   ```
2. **Window Set Title Syscall (`kSysWiniosSetWindowTitle`):**
   ```cpp
   std::size_t read_len = (t_len > 0) ? std::min<std::size_t>(t_len + 1, 256u) : 256u;
   ```
3. **Clipboard Set Syscall (`kSysWiniosClipboardSet`):**
   ```cpp
   auto vres = memory::GuestMemoryValidator::copy_string(uas, text_va, text_len + 1, text);
   ```
4. **General User String Reader (`read_user_string`):**
   Updated loop to inspect up to `max_len` bytes plus the terminal null terminator at offset `max_len`:
   ```cpp
   for (std::size_t i = 0; i <= max_len; ++i) {
       if (va + i >= memory::kUserSpaceLimitVa) return false;
       char ch = 0;
       if (!uas.copyout(reinterpret_cast<const void*>(va + i), &ch, 1)) return false;
       if (ch == '\0') return true;
       if (i == max_len) return false;
       out_str.push_back(ch);
   }
   ```

---

## 4. Verification Evidence

All 10 affected native window applications were recompiled and executed:
- `stage222_20_interactive_notes_tests`: **PASSED** (Exit 51, Note saved and verified)
- `stage222_21_runtime_monitor_tests`: **PASSED** (Exit 52, Telemetry saved and verified)
- `stage223_19_calculator_tests`: **PASSED** (Exit 61, Clipboard verified: "12")
- `stage223_20_notes_v2_tests`: **PASSED** (Exit 62, Storage verified)
- `stage223_21_resource_browser_tests`: **PASSED** (Exit 63, Clipboard verified)
- `stage224_22_platform_settings_tests`: **PASSED** (Exit 81, Configuration mutations verified)
- `stage224_22_ipc_messenger_tests`: **PASSED** (Exit 82, Message round-trip verified)
- `stage225_21_device_center_tests`: **PASSED** (Exit 91, Device notifications verified)
- `stage225_21_system_monitor_tests`: **PASSED** (Exit 92, System metrics verified)
- `stage225_22_multi_app_device_tests`: **PASSED** (Multi-app integration verified)
