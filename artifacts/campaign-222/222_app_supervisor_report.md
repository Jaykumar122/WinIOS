# WinIOS Campaign 222 — Stage 222.17: Application Supervisor & Fault Isolation Report

## 1. Overview & Architectural Role

Stage 222.17 delivers the Application Supervisor (`AppLifecycleManager`) and its integration into the interactive runtime environment. In a multi-application environment, applications are subject to crashes, voluntary termination, or supervisor-initiated backgrounding/suspension. Without strict supervisor-driven resource accounting and fault teardown, crashed guest applications leak windows, compositor surfaces, event queues, and capability tokens.

The WinIOS Application Supervisor provides deterministic lifecycle state machine transitions, process-to-window and process-to-surface association tracking, and complete automatic teardown upon application exit or fault.

---

## 2. State Machine & Invariants

The supervisor enforces the standard Apple Darwin / iOS-inspired lifecycle model:

```
[Created]
    │
    ▼
[Starting]
    │
    ├───────────────┐
    ▼               ▼
[Foreground] ──► [Background] ──► [Suspended]
    │               ▲                 │
    │               │                 ▼
    └───────────────┴─────────── [Resuming]
    │
    ▼
[Terminating]
    │
    ▼
[Terminated]
```

### Invariants:
1. **Valid Transitions:** Transition validations are strictly enforced by `is_valid_transition(from, to)`. Invalid state leaps (e.g. `Foreground` directly to `Suspended`, or transitioning out of `Terminated`) are rejected with `false`.
2. **Foreground Exclusivity:** At most one application can be in `AppState::Foreground` at any given time (`foreground_pid()`). Bringing a new application to foreground automatically triggers focus and z-order updates in the Window Manager.
3. **Supervisor Notification Syscall:** Applications can notify the supervisor of state transitions via `kSysWiniosAppSupervisorNotify` (syscall number 536), bounded by permission checks.

---

## 3. Resource Association & Automated Teardown

`AppLifecycleManager` registers resources owned by each process:
- Window IDs (`associate_window(pid, window_id)`)
- Display Surface IDs (`associate_surface(pid, surface_id)`)

Upon transition to `Terminating` / `Terminated` (triggered either by normal exit, unhandled exception, or supervisor kill):
1. **Window Destruction:** Calls `WindowManager::destroy_window(wid)` for all owned windows, immediately removing them from the compositor z-order, spatial index, and focus stack.
2. **Surface Reclamation:** Calls `SurfaceResourceManager::destroy_surface(caller_pid, sid)` for all owned surfaces, returning backing memory to the physical pool and clearing accounting stats.
3. **Input & Focus Deflection:** Clears focus in `InputRouter` and redirects pointer capture to desktop root.

---

## 4. Verification & Test Evidence

Stage 222.17 verification was executed via `tests/unit/stage222_17_app_supervisor_tests.cpp`:
- **State Transition Test:** Confirms `Created -> Starting -> Foreground -> Background -> Suspended -> Resuming -> Foreground` lifecycle sequence.
- **Fault Teardown Test:** Confirms that terminating PID 10 when owning a window and surface automatically clears `window_count()` to 0 and `surface_count` to 0.
- **Syscall Integration Test:** Confirms that `kSysWiniosAppSupervisorNotify` (536) transitions state correctly through EL0 trap dispatch.

All tests passed with zero errors and zero warnings under MSVC `/W4 /WX /permissive-`.
