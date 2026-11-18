# WinIOS Campaign 228: Native ARM64 Guest Workloads & Verification Evidence

## 1. Workload Directory & Profiles

Campaign 228 delivers two freestanding ARM64 Mach-O guest workloads cross-compiled on Windows 11 host with Zig 0.17.0 targeting `aarch64-macos`:

1. **WinIOS System Settings (`campaign228_settings_app.c`):**
   - **Bundle ID:** `com.winios.settings`
   - **Target PID:** 97
   - **Clean Exit Code:** 97
   - **Binary:** `build/campaign228_settings_app.macho` (51,504 bytes)
   - **Execution Role:** Window creation (480x360 at 80, 80), initial snapshot retrieval via `SYS_WINIOS_SETTINGS_GET` (581), setting mutation to Light Theme and Orange Accent via `SYS_WINIOS_SETTINGS_SET` (582), direct theme assignment via `SYS_WINIOS_THEME_SET` (584), Control Center toggle via `SYS_WINIOS_CONTROL_CENTER_TOGGLE` (585), window cleanup, clean exit.

2. **WinIOS Task & Accessibility Monitor (`campaign228_task_monitor.c`):**
   - **Bundle ID:** `com.winios.taskmonitor`
   - **Target PID:** 98
   - **Clean Exit Code:** 98
   - **Binary:** `build/campaign228_task_monitor.macho` (51,504 bytes)
   - **Execution Role:** Window creation (400x300 at 520, 80), active task enumeration via `SYS_WINIOS_TASK_ENUMERATE` (586), task switching via `SYS_WINIOS_TASK_SWITCH` (587), accessibility configuration (140% text scale, high-contrast enabled) via `SYS_WINIOS_ACCESSIBILITY_SET` (590), verification via `SYS_WINIOS_ACCESSIBILITY_GET` (589), window cleanup, clean exit.

---

## 2. Compilation Commands & Inlining

Cross-compilation pipeline:
```powershell
zig cc -target aarch64-macos -nostdlib -fno-builtin -fno-sanitize=all -O2 `
    -I guest-sdk/include `
    src/apps/campaign228_settings_app.c guest-sdk/lib/winios_runtime.c `
    -o build/campaign228_settings_app.macho

zig cc -target aarch64-macos -nostdlib -fno-builtin -fno-sanitize=all -O2 `
    -I guest-sdk/include `
    src/apps/campaign228_task_monitor.c guest-sdk/lib/winios_runtime.c `
    -o build/campaign228_task_monitor.macho
```

Embedded C++ representations generated into:
`src/loader/embedded_campaign228_workloads.inl`
Exposing:
- `get_embedded_campaign228_settings_app()`
- `get_embedded_campaign228_task_monitor()`

---

## 3. Execution Verification in Test Harness

Both workloads were loaded and executed under strict architectural simulation:
- `stage228_21_settings_workload_tests`: Executed genuine ARM64 guest instructions via CPU emulation and UserAddressSpace MMU translation; successfully exited with code 97; mutated settings verified in host `SystemStateManager`.
- `stage228_22_multitasking_scenario_tests`: Executed genuine ARM64 guest instructions; successfully exited with code 98; enumerated tasks and modified accessibility parameters verified in host `AccessibilityService`.
