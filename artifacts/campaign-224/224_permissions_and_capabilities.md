# WinIOS Campaign 224 — Stage 224.9 & 224.10 Application Identity and Capability Enforcement

## 1. Process Identity Model
Every running guest process in WinIOS is bound to a cryptographically unique `AppIdentity` token managed by `CapabilityManager`:

```cpp
struct AppIdentity {
    std::uint32_t pid;             // Guest PID
    std::string   bundle_id;       // Canonical reverse-DNS ID (e.g. "com.winios.settings")
    std::uint32_t uid;             // User ID (501 standard mobile user)
    std::uint64_t security_token;  // Ephemeral 64-bit verification token
};
```

---

## 2. Capability Matrix
| Capability Flag | Hex Bitmask | Description / Service Boundary Enforced |
| :--- | :---: | :--- |
| `DisplayWindow` | `0x0001` | Window creation and presentation on desktop compositor |
| `StoragePrivate` | `0x0002` | Read/write access to application's `/data/<bundle_id>/` |
| `StorageShared` | `0x0004` | Cross-application shared storage access |
| `ClipboardRead` | `0x0008` | Access to system clipboard contents |
| `ClipboardWrite` | `0x0010` | Modifying system clipboard contents |
| `ProcessSpawn` | `0x0020` | Launching child processes |
| `FilesystemAccess` | `0x0040` | Direct VFS operations |
| `IpcAccess` | `0x0080` | Creating/sending messages on guest IPC router |
| `TimerAccess` | `0x0100` | Hardware/virtual timer creation |
| `LoggingAccess` | `0x0200` | Emitting diagnostics log records |
| `ResourceAccess` | `0x0400` | Opening embedded package resources |
| `ConfigAccess` | `0x0800` | Querying and mutating application configuration |
| `DiagnosticsAccess` | `0x1000` | Inspecting platform metrics, logs, and crash reports |
| `MetadataAccess` | `0x2000` | Querying installed application registry metadata |

---

## 3. Enforcement & Audit Logging
- Every permission check increments `total_checks`.
- Denials increment `total_denials` and log the caller's PID, claimed identity, and requested capability.
- Guest syscalls obtain caller PID directly from the executing thread's `BsdProcess::pid()`, preventing guest callers from spoofing foreign PIDs.
- Cross-application storage queries without `StorageShared` are immediately rejected with `EPERM` / `AccessDenied`.
