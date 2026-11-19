# WinIOS Guest SDK v2.4 Developer Guide & Ecosystem Reference

## 1. Platform Architecture Overview

The WinIOS Guest SDK v2.4 provides comprehensive C APIs, ABI definitions, packaging standards, and developer tooling for building native ARM64 freestanding user-space applications hosted on the Windows 11 WinIOS research platform.

> [!NOTE]
> **Scope & Environment Disclaimer:**
> WinIOS is an architectural research emulation environment modeling the ARM64 EL0 application layer of an iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`, 16 KB page granule).
> WinIOS is hosted on Windows 11 x64 and does not use proprietary Apple firmware, private frameworks, or restricted binaries. It does not provide UIKit, SwiftUI, SpringBoard, Cocoa Touch, or physical Apple hardware compatibility.

---

## 2. SDK Versioning & ABI Constants

```c
#define WINIOS_SDK_VERSION_MAJOR 2
#define WINIOS_SDK_VERSION_MINOR 4
#define WINIOS_SDK_VERSION_PATCH 0
#define WINIOS_SDK_VERSION       0x00020400

#define WINIOS_CATEGORY_C_ABI_VERSION_2_4 0x00020400
#define WINIOS_CATEGORY_C_ABI_VERSION_2_3 0x00020300
#define WINIOS_CATEGORY_C_ABI_VERSION_2_2 0x00020200

#define SYS_WINIOS_APP_INFO 592
#define SYS_WINIOS_APP_LIST 593
```

---

## 3. Category C Syscall Table (Syscalls 581–593)

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
| `592` | `SYS_WINIOS_APP_INFO` | Query installed application registration | `arg0`: bundle ID string pointer, `arg1`: pointer to `winios_app_info_t` |
| `593` | `SYS_WINIOS_APP_LIST` | Enumerate installed applications | `arg0`: pointer to `winios_app_list_req_t` |

---

## 4. Category C ABI v2.4 Data Structures

All structures adhere to 8-byte alignment (`#pragma pack(push, 8)`) and compile-time size assertions.

### 4.1 Application Information Structure (`winios_app_info_t`, 128 bytes)

```c
#pragma pack(push, 8)
typedef struct {
    char     bundle_id[64];       // Application bundle ID
    char     name[32];            // Short application name
    char     version[16];         // Application semver string
    uint32_t state;               // 0=Installed, 1=Upgrading, 2=Corrupted, 3=Uninstalled
    uint32_t min_sdk_version;     // Required SDK version
    uint64_t storage_quota_bytes; // Storage budget quota in bytes
} winios_app_info_t;
#pragma pack(pop)
```

### 4.2 Application Enumerate Request Structure (`winios_app_list_req_t`, 24 bytes)

```c
#pragma pack(push, 8)
typedef struct {
    uint32_t active_only;       // 1 = active apps only, 0 = all installed
    uint32_t max_entries;       // Buffer capacity in entries
    uint32_t returned_entries;  // Host-populated count of entries returned
    uint32_t total_count;       // Host-populated total installed count
    uint64_t buffer_va;         // Guest virtual address of `winios_app_info_t` array
} winios_app_list_req_t;
#pragma pack(pop)
```

---

## 5. Package Format v3 (`WIOSPKG3`) Architecture

Package format v3 provides a cryptographically verified single-binary container for distributing freestanding ARM64 guest applications.

### 5.1 Binary Container Layout
- **Magic (8 bytes)**: `WIOSPKG3` (`0x57, 0x49, 0x4F, 0x53, 0x50, 0x4B, 0x47, 0x33`).
- **Format Version (4 bytes)**: Little-endian `0x00000003`.
- **Manifest Length (4 bytes)**: Byte length of UTF-8 INI manifest.
- **Manifest Data**: Key-value manifest body.
- **Entry Count (4 bytes)**: Number of packaged files.
- **File Entries Table**:
  - `path_len` (2 bytes) + `path` (relative VFS destination).
  - `data_size` (8 bytes).
  - `sha256_digest` (32 bytes raw SHA-256 digest).
- **Data Payloads**: Sequential raw bytes of all packaged files.

### 5.2 Trust Evaluation Policy
Packages are categorized into deterministic trust tiers:
- **System**: Built-in core system applications.
- **Developer**: Local unsigned development packages (requires developer mode enabled).
- **Untrusted**: Packages with invalid signatures or restricted capabilities.

---

## 6. Application Manifest Specification

The application manifest (`manifest.ini`) specifies application metadata, entry points, capabilities, and file associations.

```ini
format_version=3.0
bundle_id=com.example.editor
name=TextEditor
display_name=Text Editor Pro
version=1.0.0
min_os_version=230.0
architecture=arm64
min_abi_version=2.4
min_sdk_version_u32=132096
target_abi_version=2.4
executable_name=bin/editor
category=Productivity
author=WinIOS Developer
description=Freestanding text editor for WinIOS
permissions=SettingsRead,DocumentCreate,DocumentRead,DocumentWrite,DocumentShare
supported_doc_types=text/plain,.txt,.md
```

---

## 7. Developer Tooling & CLI Reference (`winios-cli`)

The `winios-cli` suite provides command-line developer workflows for inspecting, packaging, installing, launching, and debugging applications.

```text
Usage: winios-cli <command> [options]

Commands:
  pkg-inspect <pkg_path>          Inspect package headers and files
  pkg-validate <pkg_path>         Validate package integrity and trust level
  app-list [--active-only]        List installed applications
  app-info <bundle_id>            Display detailed application metadata
  app-status <bundle_id>          Check runtime state of an application
  app-install <pkg_path>          Install application package
  app-uninstall <bundle_id>       Uninstall application
  app-launch <bundle_id>          Launch application into active session
  app-terminate <pid>             Terminate running process
  diag-query                      Query recent diagnostic logs
  help                            Display this help menu
```

---

## 8. Multi-Application Lifecycle, IPC & Document Association

### 8.1 12-Step Unified Launch Pipeline
1. Resolve application identity in `ApplicationRegistry`.
2. Verify installation integrity in VFS (`/Applications/<bundle_id>.app`).
3. Validate ARM64 Mach-O header (`MH_MAGIC_64`, `CPU_TYPE_ARM64`).
4. Validate SDK and ABI compatibility constraints.
5. Verify requested capabilities and enforce restrictions.
6. Allocate process identity with monotonic generation counter.
7. Create OS task entry in `TaskManager`.
8. Enforce memory and window resource budget in `ResourceBudgetManager`.
9. Bind security capabilities to PID.
10. Record structured diagnostic launch event.
11. Populate launch result with `ProcessHandle`.
12. Handoff to scheduler and execution engine.

### 8.2 Session Management & Window Switching
The `MultiAppSessionController` coordinates multi-app sessions:
- Maintains MRU (Most Recently Used) list of running applications.
- Suspends background applications when capacity limits are reached.
- Promotes background applications on foreground exit.

### 8.3 Inter-Process Communication (IPC)
The `IpcCapabilityBroker` governs cross-process messaging:
- Communication rights granted via unforgeable 64-bit capability tokens.
- Strict sender and receiver handle validation with generation counters preventing stale PID reuse attacks.
- Hard bounded message queue depth per process port (max 32 messages, max 4096 bytes per message).

### 8.4 Document Association & Handoff
The `DocumentAssociationManager` integrates package manifests with document access:
- Resolves handlers by MIME type and file extension.
- Supports default handler overrides and fallback candidate lists.
- Enforces permission-preserving access grants without authorization forgery.
- Automatically revokes grants and association entries upon application uninstallation.
