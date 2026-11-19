# Category C ABI v2.4 Conformance & Syscall Specification

## 1. Scope & ABI Versioning

WinIOS Category C ABI v2.4 (`0x00020400`) standardizes the supervisor call boundary between freestanding ARM64 guest applications and the host runtime.

- **Architectural Calling Convention**: ARM64 standard register convention (`AArch64 AAPCS`).
- **Syscall Trigger**: `SVC #0x80` with syscall number passed in register `X16`.
- **Arguments**: Passed in registers `X0` through `X5`.
- **Return Values**:
  - `X0`: Return status code (`0` on success, or Darwin POSIX error code `ENOENT`, `EFAULT`, `EINVAL`, `ENOSYS`).
  - `X1` / `X2`: Secondary return scalar values (e.g. byte counts, handles).
  - `X3`: Error indicator / Darwin error code when `X0 == -1`.

---

## 2. Category C Syscall Table (Syscalls 500..593)

| Syscall Number | Symbolic Name | Arguments | Description |
|---|---|---|---|
| **500..512** | Core UI & Windowing | `X0..X3` | Window creation, frame buffer configuration, event pump, destruction. |
| **513..529** | Audio & Input | `X0..X2` | Audio playback, keyboard/touch input dispatch, clipboard operations. |
| **530..570** | Desktop & Compositor | `X0..X4` | Z-order stacking, focus management, window animator, display surfaces. |
| **571..580** | Document Services | `X0..X3` | Document picker, file stream read/write/seek, access grant delegation. |
| **581** | `SYS_WINIOS_SETTINGS_GET` | `X0` (key), `X1` (val_out), `X2` (len) | Retrieve system preference or configuration value. |
| **582** | `SYS_WINIOS_SETTINGS_SET` | `X0` (key), `X1` (val), `X2` (len) | Set system preference or configuration value. |
| **583** | `SYS_WINIOS_THEME_GET` | `X0` (theme_out) | Retrieve active UI theme (Dark/Light/HighContrast). |
| **584** | `SYS_WINIOS_THEME_SET` | `X0` (theme_id) | Switch system UI theme. |
| **585** | `SYS_WINIOS_CONTROL_CENTER_TOGGLE` | `X0` (module_mask) | Toggle system control center modules (Wi-Fi, Bluetooth, DND). |
| **586** | `SYS_WINIOS_TASK_ENUMERATE` | `X0` (buf), `X1` (max_count) | Enumerate active running tasks and background states. |
| **587** | `SYS_WINIOS_TASK_SWITCH` | `X0` (pid) | Request task foreground focus switch. |
| **588** | `SYS_WINIOS_TASK_TERMINATE` | `X0` (pid), `X1` (graceful) | Terminate target task gracefully or forcibly. |
| **589** | `SYS_WINIOS_ACCESSIBILITY_QUERY` | `X0` (query_req) | Query assistive technology settings (ScreenReader, LargeText). |
| **590** | `SYS_WINIOS_ACCESSIBILITY_SET` | `X0` (config) | Configure accessibility parameters. |
| **591** | `SYS_WINIOS_STATE_SNAPSHOT` | `X0` (snapshot_type) | Trigger crash-consistent system state snapshot. |
| **592** | `SYS_WINIOS_APP_INFO` | `X0` (bundle_id_ptr), `X1` (info_buf_ptr) | Query application metadata record (128 bytes). |
| **593** | `SYS_WINIOS_APP_LIST` | `X0` (req_buf_ptr) | Enumerate installed applications into user buffer. |

### Syscall Ceiling Enforcement
Any syscall number `>= 594` is strictly unmapped and returns:
- `X0 = -1`
- `X3 = 78` (`ENOSYS`: Function not implemented)

---

## 3. ABI Data Structures & Alignment

### `winios_app_info_t` (Syscall 592 — 128 Bytes, 8-Byte Aligned)
```c
typedef struct {
    char     bundle_id[64];       // Null-terminated bundle identifier
    char     bundle_name[32];     // Human-readable application name
    char     version_string[16];  // SemVer version string (e.g. "1.0.0")
    uint32_t sdk_version;         // Target SDK version (e.g. 0x00020400)
    uint32_t installation_state;  // 0: Uninstalled, 1: Installed, 2: Suspended
    uint64_t disk_usage_bytes;    // Size of sandbox files on disk
} winios_app_info_t;
```

### `winios_app_list_req_t` (Syscall 593 — 24 Bytes, 8-Byte Aligned)
```c
typedef struct {
    uint64_t buffer_ptr;          // User virtual address of destination array
    uint32_t buffer_size_bytes;   // Byte capacity of destination buffer
    uint32_t count_returned;      // Number of records populated by kernel
    uint32_t total_installed;     // Total count of registered apps in registry
    uint32_t reserved;            // Alignment padding (must be 0)
} winios_app_list_req_t;
```

---

## 4. Copyin / Copyout Boundary Safety

The user syscall dispatcher enforces strict boundary validations prior to reading from or writing to guest user memory:
1. **Kernel Address Space Barrier**: Pointers with the high bit set (`va >= 0x8000000000000000ULL`) are rejected with `EFAULT`.
2. **Buffer Overflow Protection**: Struct operations verify `va + sizeof(struct) <= kUserSpaceLimit` using checked addition to prevent arithmetic wrapping.
3. **Null Pointer Rejection**: Zero pointers are rejected immediately with `EFAULT`.
