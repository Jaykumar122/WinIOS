# WinIOS Guest SDK v2.3 Developer Guide & ABI Specification

## 1. Platform Architecture Overview

The WinIOS Guest SDK v2.3 provides C APIs and ABI definitions for developing native ARM64 freestanding user-space applications hosted on the Windows 11 WinIOS research platform.

> [!NOTE]
> **Scope & Environment Disclaimer:**
> WinIOS is an architectural research emulation environment modeling the ARM64 EL0 application layer of an iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`, 16 KB page granule).
> WinIOS is hosted on Windows 11 x64 and does not use proprietary Apple firmware, private frameworks, or restricted binaries. It does not provide UIKit, SwiftUI, SpringBoard, or physical Apple hardware compatibility.

---

## 2. SDK Versioning & ABI Constants

```c
#define WINIOS_SDK_VERSION_MAJOR 2
#define WINIOS_SDK_VERSION_MINOR 3
#define WINIOS_SDK_VERSION_PATCH 0
#define WINIOS_SDK_VERSION       0x00020300

#define WINIOS_CATEGORY_C_ABI_VERSION_2_3 0x00020300
#define WINIOS_CATEGORY_C_ABI_VERSION_2_2 0x00020200
```

---

## 3. Category C Syscall Table (Syscalls 581–591)

| Syscall Number | Constant | Description | Primary Arguments |
|:---|:---|:---|:---|
| `581` | `SYS_WINIOS_SETTINGS_GET` | Query full system settings snapshot | `arg0`: pointer to `winios_settings_snapshot_t` |
| `582` | `SYS_WINIOS_SETTINGS_SET` | Mutate setting by numeric key | `arg0`: key ID, `arg1`: 64-bit value |
| `583` | `SYS_WINIOS_THEME_GET` | Query current theme & accent | `arg0`: out theme pointer, `arg1`: out accent pointer |
| `584` | `SYS_WINIOS_THEME_SET` | Set theme mode & accent color | `arg0`: theme mode (0=Light, 1=Dark, 2=System), `arg1`: accent color |
| `585` | `SYS_WINIOS_CONTROL_CENTER_TOGGLE` | Toggle or query Control Center | `arg0`: action (0=toggle, 1=show, 2=hide) |
| `586` | `SYS_WINIOS_TASK_ENUMERATE` | Enumerate active tasks | `arg0`: buffer pointer, `arg1`: max count, `arg2`: out count |
| `587` | `SYS_WINIOS_TASK_SWITCH` | Switch focus or cycle foreground | `arg0`: target PID (0 to cycle) |
| `588` | `SYS_WINIOS_TASK_TERMINATE` | Terminate task with lifecycle reclamation | `arg0`: target PID, `arg1`: graceful flag (1=graceful, 0=force) |
| `589` | `SYS_WINIOS_ACCESSIBILITY_GET` | Query accessibility state | `arg0`: pointer to `winios_accessibility_info_t` |
| `590` | `SYS_WINIOS_ACCESSIBILITY_SET` | Mutate accessibility parameters | `arg0`: text scale (100..200%), `arg1`: high contrast flag |
| `591` | `SYS_WINIOS_DIAG_EVENT_QUERY` | Query structured diagnostic event stream | `arg0`: pointer to `winios_diag_query_req_t` |

---

## 4. Category C ABI v2.3 Data Structures

All Category C ABI structures use explicit byte packing (`#pragma pack(push, 8)`) and compile-time size verification.

### 4.1 Diagnostic Event Record (`winios_diag_event_record_t`, 48 bytes)

```c
#pragma pack(push, 8)
typedef struct {
    uint64_t sequence;        // Monotonic sequence identifier
    uint64_t timestamp_us;    // Virtual microseconds timestamp
    uint32_t pid;             // Originating task PID
    uint32_t level;           // Severity (0=Debug, 1=Info, 2=Warn, 3=Error, 4=Fatal)
    uint32_t category;        // Event Category (1=System, 2=Kernel, 3=Security, etc.)
    int32_t  error_code;      // Platform error code if applicable
    char     tag[16];         // Subsystem tag or bundle ID (null-terminated)
} winios_diag_event_record_t;
#pragma pack(pop)
```

### 4.2 Diagnostic Query Request (`winios_diag_query_req_t`, 32 bytes)

```c
#pragma pack(push, 8)
typedef struct {
    uint64_t after_sequence;    // Filter: query records with sequence > after_sequence
    uint32_t category_filter;   // Filter: category ID (0 = all categories)
    uint32_t min_level;         // Filter: minimum severity level
    uint32_t max_entries;       // Request: max entries (bounded to 128 per call)
    uint32_t returned_entries;  // Response: actual records returned by host
    uint64_t buffer_va;         // Pointer: guest virtual address to receive records
} winios_diag_query_req_t;
#pragma pack(pop)
```

### 4.3 Settings Snapshot (`winios_settings_snapshot_t`, 48 bytes)

```c
#pragma pack(push, 8)
typedef struct {
    uint32_t theme_mode;          // 0=Light, 1=Dark, 2=System
    uint32_t accent_color;        // 0=Blue, 1=Purple, 2=Pink, 3=Red, 4=Orange, 5=Yellow, 6=Green, 7=Graphite
    uint32_t display_width;       // Default: 800
    uint32_t display_height;      // Default: 600
    uint32_t display_scale;       // 100, 125, 150, 200
    uint32_t display_orientation; // 0=Landscape, 1=Portrait
    uint32_t master_volume;       // 0..100
    uint32_t is_muted;            // 0 or 1
    uint32_t low_power_mode;      // 0 or 1
    uint32_t text_scale_percent;  // 100..200
    uint32_t high_contrast;       // 0 or 1
    uint32_t network_enabled;     // 0 or 1
} winios_settings_snapshot_t;
#pragma pack(pop)
```

---

## 5. Security & Isolation Model

WinIOS enforces capability checks at the host kernel boundary:
- **`Capability::SettingsRead`:** Required to query system settings snapshot (`SYS_WINIOS_SETTINGS_GET`).
- **`Capability::SettingsWrite`:** Required to mutate system settings (`SYS_WINIOS_SETTINGS_SET`, `SYS_WINIOS_THEME_SET`).
- **`Capability::TaskManage`:** Required to enumerate or terminate other processes. Callers without this capability only see their own process in task listings and can only terminate themselves.
- **`Capability::AccessibilityAccess`:** Required to configure global accessibility settings (`SYS_WINIOS_ACCESSIBILITY_SET`).
- **`Capability::DiagnosticsAccess`:** Grants privileged access to all system-wide diagnostic records across all processes. Unprivileged callers can only query their own PID's records or system-level notices (PID 0).

---

## 6. Cross-Compilation Pipeline

Compile freestanding ARM64 Mach-O binaries using Zig 0.17.0:

```powershell
zig cc -target aarch64-macos -nostdlib -fno-builtin -fno-sanitize=all -O2 `
    -I guest-sdk/include `
    src/apps/my_app.c guest-sdk/lib/winios_runtime.c `
    -o build/my_app.macho
```
