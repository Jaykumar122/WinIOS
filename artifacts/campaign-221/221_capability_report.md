# WinIOS Application Sandbox and Capability Model Report

**Campaign:** 221 (Stage 221.10)  
**Subsystem:** `CapabilityManager` & Least-Privilege Enforcement  

---

## 1. Capability Enumeration

WinIOS enforces a bitmask capability model:

| Capability Bit | Name | Description |
|---|---|---|
| `1 << 0` | `DisplayWindow` | Allows creation of desktop windows and surface presentation. |
| `1 << 1` | `StoragePrivate` | Read/write access within the application's isolated `/data/<app_id>/` directory. |
| `1 << 2` | `StorageShared` | Read access to common public data partitions. |
| `1 << 3` | `ClipboardRead` | Read clipboard contents. |
| `1 << 4` | `ClipboardWrite` | Write data to system clipboard. |
| `1 << 5` | `ProcessSpawn` | Ability to launch child guest processes. |
| `1 << 6` | `FilesystemAccess` | General VFS operations outside private storage. |
| `1 << 7` | `IpcAccess` | Creation of IPC ports and message exchange. |
| `1 << 8` | `TimerAccess` | Creation of virtual hardware timers. |
| `1 << 9` | `LoggingAccess` | Structured guest diagnostic logging. |
| `1 << 10` | `ResourceAccess` | Access to bundled application assets. |

---

## 2. Default Sandbox Policy

1. **Unprivileged Guest Applications:** Granted `StoragePrivate`, `DisplayWindow`, and `LoggingAccess` by default.
2. **Elevated Applications:** Must declare capabilities in package manifest (`permissions=display.window,storage.private,process.spawn`).
3. **PID 1 (Launchd / System Shell):** Implicitly granted full capability mask (`0xFFFFFFFF`).
4. **Enforcement Point:** Intercepted at syscall dispatch in `UserSyscallDispatcher`. Unauthorized calls immediately return `kErrnoEPERM` without side effects.
