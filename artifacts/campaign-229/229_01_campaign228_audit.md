# WinIOS Campaign 229 — Stage 229.1 Architecture and Security Audit

## 1. Executive Summary

A comprehensive architectural and security audit was conducted on the WinIOS Campaign 228 baseline (`a4935c861982b1dace290147c6c4e70ab91ed2f2`), covering:
- `SystemStateManager` & `SettingsSchema`
- `AppearanceService` & `ControlCenterService`
- `TaskManager` & `AccessibilityService`
- Category C ABI v2.2 Syscalls (581..590)
- Guest SDK v2.2 and UserAddressSpace syscall dispatch
- Persistence and notification infrastructure

The audit identified 8 key architectural and security findings spanning capability enforcement, persistence durability, observer reentrancy, task lifecycle resource reclamation, and syscall error propagation.

---

## 2. Detailed Findings Matrix

| Finding ID | Subsystem | Classification | Severity | Impact Summary |
|---|---|---|---|---|
| **SEC-229-01** | Syscall Dispatcher | Security / Capabilities | **HIGH** | Syscalls 581..590 lack capability enforcement; untrusted guest apps can kill arbitrary tasks or alter platform settings. |
| **SEC-229-02** | SystemStateManager / VFS | Durability / Crash-Safety | **HIGH** | `save_to_vfs` does not perform atomic rename; interrupted writes can truncate `/System/Library/Preferences/system_settings.ini`. |
| **REL-229-03** | SystemStateManager | Transactional Consistency | **MEDIUM** | Observers notified before `save_to_vfs`; on persistence failure, memory state is not rolled back. |
| **REL-229-04** | SystemStateManager | Concurrency / Reentrancy | **MEDIUM** | Observer callbacks modifying observer list during notification cause vector reallocation and iterator invalidation. |
| **REL-229-05** | TaskManager | Concurrency / Reentrancy | **MEDIUM** | `terminate_task` holds iterator across external `lifecycle_handler_` invocation; vector mutation leads to stale iterator write. |
| **RES-229-06** | TaskManager | Resource Management | **MEDIUM** | Graceful/force task termination does not reclaim associated window handles, document streams, or subscriptions. |
| **ABI-229-07** | Syscall Dispatcher | ABI / Memory Boundary | **LOW** | `SYS_WINIOS_TASK_ENUMERATE` ignores return value of `uas.copyin(arg2, ...)`, failing to report `EFAULT` on bad output pointers. |
| **ERR-229-08** | Platform Infrastructure | Error Model | **LOW** | Heterogeneous error representation across services; absence of unified error categories and diagnostics integration. |

---

## 3. Analysis & Reproducible Scenarios

### Finding SEC-229-01: Capability Enforcement on Syscalls 581..590
- **Location:** `src/kernel/user_syscall_dispatcher.cpp` lines 2684–2920.
- **Evidence:** Syscall handler checks only arguments (null pointers, enum values) but does not query `CapabilityManager` for permissions such as `CAP_SYSTEM_SETTINGS`, `CAP_TASK_MANAGE`, or `CAP_ACCESSIBILITY`. A sandboxed guest app can terminate any PID or toggle network adapters.
- **Remediation Plan:** Check capability masks against calling `proc.bundle_id()` or capability token; reject with `kErrnoEPERM` when unauthorized.

### Finding SEC-229-02: Crash-Safe Settings Persistence
- **Location:** `src/settings/system_state_manager.cpp` lines 200–221.
- **Evidence:**
  ```cpp
  const std::string tmp_path = vfs_path + ".tmp";
  vfs_->write_file(tmp_path, serialized.data(), serialized.size());
  vfs_->write_file(vfs_path, serialized.data(), serialized.size());
  vfs_->unlink(tmp_path);
  ```
  The secondary write directly overwrites `vfs_path`. An interruption or host power failure leaves `system_settings.ini` zero-length or malformed.
- **Remediation Plan:** Implement atomic replacement via VFS (`vfs_->rename(tmp, target)`), deterministic temporary file naming (`system_settings.ini.tmp.<pid>.<seq>`), and backup recovery (`system_settings.ini.bak`).

### Finding REL-229-03: Transactional Rollback on Persistence Failure
- **Location:** `src/settings/system_state_manager.cpp` lines 66–72.
- **Evidence:**
  ```cpp
  SystemSettings prev = settings_;
  settings_ = tentative;
  notify_observers_locked(prev, settings_);
  save_to_vfs();
  ```
  If `save_to_vfs()` returns `false`, `settings_` remains modified and observers already acted on uncommitted state.
- **Remediation Plan:** Perform `save_to_vfs()` *before* observer notification. If persistence fails, restore `settings_ = prev` and return `false` without notifying observers.

### Finding REL-229-04: Observer Reentrancy Hazard
- **Location:** `src/settings/system_state_manager.cpp` line 159.
- **Evidence:** Direct iteration over `observers_` while holding `recursive_mutex` allows an observer's `on_appearance_changed` to call `add_observer` or `remove_observer`, invalidating the vector.
- **Remediation Plan:** Snapshot the observer list into a local copy before dispatching callbacks, or queue observer registrations during active notification loops.

### Finding REL-229-05: TaskManager Iterator Invalidation
- **Location:** `src/settings/task_manager.cpp` lines 164–185.
- **Evidence:**
  ```cpp
  auto it = std::find_if(...);
  lifecycle_handler_(pid, graceful);
  it->state = TaskState::Suspended;
  ```
  Calling `lifecycle_handler_` while holding iterator `it` leads to undefined behavior if the handler unregisters or registers tasks.
- **Remediation Plan:** Do not store an iterator across the handler call. Re-lookup the task by PID after the handler completes.

### Finding RES-229-06: Resource Reclamation on Termination
- **Location:** `src/settings/task_manager.cpp` & `src/kernel/user_syscall_dispatcher.cpp`.
- **Evidence:** When PID 97 or 98 exits or is terminated, windows owned by the task are not automatically destroyed if the app crashed, potentially leaving orphaned display surfaces.
- **Remediation Plan:** Introduce a task cleanup coordinator that closes open document streams, destroys task windows, and releases subscriptions upon task exit/termination.

---

## 4. Audit Acceptance & Next Steps

All 8 findings are documented with actionable remediation paths. The subsequent stages (229.2 through 229.22) directly resolve each finding in priority order without breaking existing test guarantees.
