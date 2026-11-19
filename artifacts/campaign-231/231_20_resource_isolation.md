# Stage 231.20: Multi-App Scheduling, Suspension, and Resource Isolation

## Overview
Stage 231.20 audited and verified multi-application lifecycle orchestration (`MultiAppSessionController`), per-process resource budget limits (`ResourceBudgetManager`), state-machine transitions (Foreground, Background, Suspended, Terminated), LRU eviction, crash recovery, and post-termination unbinding of capability tokens and document grants.

## Key Verified Mechanisms

1. **Session Lifecycle & LRU Eviction**:
   - Scheduling execution model: cooperative, event-driven application state-machine transitions (`Foreground`, `Background`, `Suspended`, `Terminated`).
   - Launching an application automatically promotes it to `Foreground` and transitions previously active application to `Background`.
   - When active application capacity (`max_active_apps = 2`) is reached, the least recently used background application is automatically transitioned to `Suspended`.
   - Live capability ownership and process handles remain valid throughout suspension.
   - Resuming a suspended application promotes it to `Foreground`, evicting the oldest background application if at capacity.
   - State snapshots (`saved_state_snapshot`) preserved during suspension.

2. **Automatic Foreground Promotion & Failure Recovery**:
   - Terminating a foreground process automatically detects the most recently focused background application and promotes it to `Foreground`.
   - When all processes are suspended or terminated, `foreground_pid()` cleanly resolves to 0.

3. **Per-Process Resource Budget Isolation (`ResourceBudgetManager`)**:
   - Enforced memory limits (16 MB default per task), window limits (8 default), and document stream limits (16 default).
   - Attempted over-allocation by one process (e.g. PID A requesting 20 MB) fails deterministically with `PlatformErrorCategory::ResourceExhausted`.
   - Cross-process isolation confirmed: resource exhaustion in PID A has zero effect on PID B's ability to allocate resources.
   - `release_all_task_resources(pid)` completely reclaims memory, window, and stream counters for the terminated PID without touching adjacent processes.

4. **Termination Cleanup of IPC & Document Access**:
   - Terminating a process handle invalidates subsequent IPC send operations (`IpcStatus::SenderInvalid`).
   - Terminating a process handle invalidates document access checks (`check_access` returns false) and ensures grant tokens cannot be reused by stale generations.

## Test Results
- **Test Target**: `stage231_20_resource_isolation_tests` (CTest #922)
- **RelWithDebInfo**: Passed (0.05s)
- **Debug**: Passed (0.06s)
