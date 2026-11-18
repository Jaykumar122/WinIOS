# WinIOS Campaign 222 — Stage 222.18: Persistent Application State & Sandbox Recovery Report

## 1. Overview & Architectural Role

Stage 222.18 provides persistent application state management, data serialization, and sandbox isolation guarantees across application life cycles and process restarts.

For interactive applications (e.g. `campaign222_interactive_notes`), application state (such as note text, toggle states, window positions) must persist across process termination and reboot. Crucially, the security model mandates that each guest application can only read and write within its own containerized sandbox directory (`/data/<app_id>/`), preventing malicious or errant applications from inspecting or corrupting data belonging to other applications or system directories.

---

## 2. Sandbox Isolation & Virtual File System Integration

The `IsolatedStorage` subsystem operates on top of the WinIOS `VirtualFileSystem`:
- **Base Container Path:** Each application is jailed to `/data/<app_id>/` (e.g. `/data/com.apple.mobilenotes/`).
- **Path Sanitization & Traversal Prevention:** Any path containing relative navigation elements (`..`, `.` components traversing upward) or absolute system root targets (`/etc/`, `/System/`) is strictly prohibited. `is_path_safe(path)` guarantees that resolution stays within the container boundary.
- **Atomic File Writing:** State serialization uses atomic write semantics (temporary write followed by atomic rename) to prevent state corruption in the event of an abrupt process crash or VM halt.

---

## 3. Integration with Guest SDK

Guest applications access storage services through the Guest SDK v1.6 APIs:
- `winios_storage_write(const char* key, const void* data, uint32_t len)`
- `winios_storage_read(const char* key, void* buf, uint32_t max_len)`
- `winios_storage_delete(const char* key)`

These APIs map to VFS syscalls (`SYS_open`, `SYS_write`, `SYS_read`, `SYS_close`, `SYS_unlink`) with container path prefixes resolved in the kernel layer according to the calling process's sandboxed `app_id`.

---

## 4. Verification Evidence

Stage 222.18 verification was verified in `tests/unit/stage222_17_app_supervisor_tests.cpp`:
- **State Write & Readback:** Wrote JSON state payload for `com.apple.mobilenotes` and successfully retrieved and verified exact bit-for-bit payload.
- **Path Traversal Protection:** Confirmed rejection of `../foreign_app/secret.txt` and `/etc/passwd`.
- **Inter-Application Isolation:** Attempted to read the notes state file from `com.apple.runtimemonitor`, confirming that access returns `StorageResult::FileNotFound` due to container root separation.

All unit tests compiled and executed cleanly under MSVC `/W4 /WX /permissive-`.
