# WinIOS Campaign 216 — Security & Boundary Isolation Report

## 1. Threat Model & Security Posture
In a multi-window guest execution environment, malicious or malfunctioning guest processes may attempt to:
1. Intercept keystrokes or mouse events intended for other processes (Input Snooping / Theft).
2. Inject synthetic events into windows owned by elevated or different processes.
3. Access or modify geometry, focus, or z-order of windows owned by foreign PIDs.
4. Exhaust host or kernel memory through unbounded event queueing or timer creation.
5. Corrupt memory by passing unmapped or misaligned pointers across the Category C ABI boundary.

---

## 2. Security Controls & Verifications

### 2.1 Input Route Isolation & Anti-Theft Protection
- **Per-PID Queue Isolation:** Each process owns a discrete `TouchEventQueue` within `InputRouter`.
- **Target PID Enforcement:** When an event is dispatched with an explicit `target_window_id`, `InputRouter::route_event` queries `WindowManager` to determine the authentic owning PID. The event is enqueued *only* to the owning PID's queue.
- **Destroyed Window Protection:** If an event targets a window ID that has been closed or destroyed, it is immediately discarded with `InputDeliveryStatus::RejectedWindowDestroyed`, preventing stale event delivery to recycled IDs.
- **Queue Backpressure & Overflow Guard:** Each input queue has a strict capacity limit (e.g., 64 events). When full, incoming events are rejected with `InputDeliveryStatus::DroppedQueueFull`, preventing memory exhaustion attacks.

### 2.2 Category C ABI v1.1 Validation & Memory Guard
- **Strict Pointer Validation:** All pointers passed to syscalls 508..512 are verified against `memory::GuestMemoryValidator`.
- **Alignment Enforcement:** Pointers to `winios_window_geometry_t`, `winios_window_list_t`, and `winios_event_t` must be 8-byte aligned. Unaligned or unmapped user addresses immediately return `EFAULT` (14) without touching guest memory.
- **Cross-Process Window Access Guard:** Syscalls `kSysWiniosWindowGetGeometry` (508), `kSysWiniosWindowSetGeometry` (509), and `kSysWiniosWindowSetFocus` (510) verify `window->can_access(caller_pid)`. Unauthorized access attempts return `EBADF` (9).

### 2.3 Guest Virtual Timer Resource Quotas
- **Per-Process Timer Quota:** Each guest process is limited to a maximum of 16 active timers (`kMaxTimersPerProcess = 16`). Attempting to register a 17th timer returns `EPERM` (1).
- **Process Termination Cleanup:** When a process exits or is terminated, `GuestEventServices::cleanup_process_timers(pid)` purges all associated timers, preventing orphaned timer leaks.

### 2.4 Application Sandboxing & Storage Isolation
- **Path Sanitization:** `IsolatedStorage::is_path_safe` rejects relative path traversal attempts containing `..`, absolute roots, or illegal characters.
- **Atomic File Operations:** Writing documents executes via atomic staging, flushing, and renaming, ensuring document consistency and preventing torn writes.
