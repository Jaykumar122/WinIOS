# WinIOS Campaign 222 — Stage 222.10: Dynamic Resizing, Surface Invalidation & Redraw Coordination Report

**Campaign:** 222  
**Stage:** 222.10  
**Date:** 2026-11-18  
**Component:** Window Manager & Lifecycle Subsystem (`src/graphics/window_manager.h`, `src/graphics/window_manager.cpp`, `src/kernel/user_syscall_dispatcher.cpp`)  
**Status:** COMPLETE (100% test pass rate)

---

## 1. Executive Summary

Stage 222.10 hardens the dynamic window lifecycle across window repositioning, frame resizing, client surface reallocation, damage unioning, non-client zone hit testing, and Category C guest syscall dispatch.

Key outcomes achieved:
1. **Dynamic Frame Resizing & Clamping:** Windows dynamically adapt to size requests via `WindowManager::resize_window` and `WindowManager::set_window_frame`, enforcing configurable constraints ($min\_w \ge 100$, $min\_h \ge 80$, $max\_w \le 3840$, $max\_h \le 2160$).
2. **Client Surface Adaptation:** On window resizing, the underlying `DisplaySurface` automatically reallocates its pixel buffer and stride to match the updated `client_frame()`.
3. **Damage Union Tracking:** Moving or resizing a window computes the spatial bounding union of the pre-transformation and post-transformation frames:
   $$R_{\text{damage}} = R_{\text{old}} \cup R_{\text{new}}$$
   ensuring the compositor cleans up vacated desktop regions without artifact ghosting.
4. **Non-Client Hit-Testing:** Precise hit-testing resolves non-client zones (Title Bar, Close Button, Maximize Button, Minimize Button, Border, and Client area) for window management interactions.
5. **Guest Syscall Integration:** Category C system call `kSysWiniosWindowSetGeometry` (531) validates guest memory buffers and applies frame, z-order, and pinned state with permission checks.

---

## 2. Technical Architecture & Invariants

### 2.1 Spatial Damage Calculation
When a window transitions from frame $F_0 = (x_0, y_0, w_0, h_0)$ to $F_1 = (x_1, y_1, w_1, h_1)$:
$$x_d = \min(x_0, x_1), \quad y_d = \min(y_0, y_1)$$
$$x_{\max} = \max(x_0 + w_0, x_1 + w_1), \quad y_{\max} = \max(y_0 + h_0, y_1 + h_1)$$
$$w_d = x_{\max} - x_d, \quad h_d = y_{\max} - y_d$$
The dirty rectangle $R_{\text{damage}} = (x_d, y_d, w_d, h_d)$ is accumulated into `dirty_rect_` and signaled to the compositor.

### 2.2 Non-Client Geometry Partitioning
Given frame $F$:
- Title bar: $y \in [F.y, F.y + 28)$
- Close button: $x \in [F.x + F.w - 24, F.x + F.w - 6)$, $y \in [F.y + 5, F.y + 23)$
- Maximize button: $x \in [F.x + F.w - 48, F.x + F.w - 30)$
- Minimize button: $x \in [F.x + F.w - 72, F.x + F.w - 54)$
- Client area: $x \in [F.x + 1, F.x + F.w - 1)$, $y \in [F.y + 28, F.y + F.h - 1)$

---

## 3. Verification & Evidence

Automated verification in `tests/unit/stage222_10_window_lifecycle_tests.cpp` established:
1. `test_dynamic_resizing_and_clamping`: Dynamic resizing updates surface size to client frame; clamping respects minimum constraints; foreign process resizing rejected.
2. `test_damage_tracking_on_move_and_resize`: Window translation by $(+50, +30)$ computes exact union damage $[100, 100, 250, 180]$.
3. `test_non_client_hit_testing`: Correctly classifies TitleBar, Client, CloseButton, and None.
4. `test_set_geometry_syscall`: kSysWiniosWindowSetGeometry (531) dispatched through `UserSyscallDispatcher` from simulated user process, updating frame, z-order, always-on-top, and marking window dirty.

**Result:** 4/4 tests passed (100%).
