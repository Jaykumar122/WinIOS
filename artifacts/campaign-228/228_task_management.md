# WinIOS Campaign 228: Task Management & Multitasking Architecture

## 1. Unified Task Model

The WinIOS Task Model (`src/settings/task_manager.h`, `.cpp`) provides process identity, window association, memory tracking, and lifecycle supervision across all active guest applications.

### 1.1 Task Descriptor (`TaskInfo` & `winios_task_entry_t`)
Each active task maintains:
- `pid`: Process identifier (uint32_t).
- `bundle_id`: Package identity string (e.g. `com.winios.settings`).
- `name`: Human-readable application title (e.g. `System Settings`).
- `primary_window`: 64-bit window server handle.
- `state`: Process execution state (`Running`, `Suspended`, `Zombie`, `Crashed`).
- `launch_time_us`: Microsecond timestamp of process creation.
- `memory_bytes`: Physical page consumption tracking.
- `is_foreground`: Boolean indicating active focused window ownership.

---

## 2. Task Switcher Overlay

The desktop includes a modal task switcher overlay (analogous to App Switcher / Command-Tab):
- **Visibility:** `show_switcher()`, `hide_switcher()`, `toggle_switcher()`.
- **Navigation:**
  - `cycle_next()`: Advances selection rightward, wrapping to 0.
  - `cycle_prev()`: Moves selection leftward, wrapping backwards.
- **Activation:** `activate_selected_task()` marks the chosen task as `is_foreground = true`, resets previous foreground tasks, and auto-dismisses the switcher overlay.

---

## 3. Lifecycle Controls & Resource Cleanup

Applications can be managed gracefully or forcefully terminated:
- **Graceful Termination (`terminate_task(pid, graceful=true)`):**
  - Sends shutdown notification / request to application event loop.
  - Transitions task state to `Suspended`.
- **Force Kill (`force_kill_task(pid)`):**
  - Transitions task state to `Zombie`.
  - Removes task from active scheduling queues.
  - Triggers window destruction via `WindowManager`.
  - Revokes active document grants and closes open streams.
  - Enforces process ownership validation; prevents arbitrary cross-process termination without root / supervisor privileges.
