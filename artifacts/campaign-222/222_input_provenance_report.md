# WinIOS Campaign 222 — Stage 222.8 & 222.9: Input Provenance, Pointer Dragging, Coordinate Translation & Focus Routing Report

**Campaign:** 222  
**Stages:** 222.8 & 222.9  
**Date:** 2026-11-18  
**Component:** Input Subsystem (`src/input/input_router.h`, `src/input/input_event.h`, `src/graphics/window_manager.h`)  
**Status:** COMPLETE (100% test pass rate)

---

## 1. Executive Summary

Stages 222.8 and 222.9 establish verified input provenance tracking, coordinate translation under aspect ratio scaling, multi-window hit testing with z-order awareness, pointer drag capture across window boundaries, and deterministic keyboard focus routing.

The host-to-guest input pipeline ensures that:
1. Every input event carries a cryptographic/provenance classification (`PhysicalWindowsInput`, `SyntheticInjected`, `ReplayAudit`, `ForeignUntrusted`).
2. Foreign processes cannot inject synthetic events into another process's event queue or forge physical provenance without supervisor privilege.
3. Pointer coordinates from Windows client space ($X_{\text{host}}, Y_{\text{host}}$) translate into guest virtual display coordinates ($X_{\text{guest}}, Y_{\text{guest}}$) and then into window-local space ($X_{\text{local}}, Y_{\text{local}}$), clamping correctly at boundaries.
4. Active pointer dragging captures the pointer to the initiating window even when the pointer moves outside the window's bounding box, until the mouse button is released.
5. Keystrokes route strictly to the active top-level focused window.

---

## 2. Technical Implementation & Architecture

### 2.1 Coordinate Translation & Aspect Scaling
The host viewport may have scaling factors ($S_x, S_y$) and offsets ($O_x, O_y$).
Given host coordinates $(x_h, y_h)$:
$$x_g = \text{clamp}\left(\frac{x_h - O_x}{S_x}, 0, W_{\text{display}} - 1\right)$$
$$y_g = \text{clamp}\left(\frac{y_h - O_y}{S_y}, 0, H_{\text{display}} - 1\right)$$

Window-local coordinates:
$$x_l = x_g - W_x$$
$$y_l = y_g - W_y$$

### 2.2 Hit-Testing and Z-Order
When a mouse button down occurs without an existing drag capture:
- Windows are evaluated from highest z-order to lowest (front-to-back in screen space).
- The first visible window whose bounding rectangle contains $(x_g, y_g)$ becomes the target.
- `WindowManager::set_focus(wid)` updates the active focused window.
- The window is brought to the top of the z-order stack.

### 2.3 Mouse Drag Capture
When a mouse down occurs on window $W$, $W$ captures the pointer:
- Subsequent mouse move events are delivered to $W$'s queue regardless of current cursor position, with window-local coordinates reflecting the displacement (even if negative or exceeding window dimensions).
- Upon button up, the event is delivered to $W$ and the pointer capture is released.

### 2.4 Keyboard Routing & Provenance Auditing
- Raw Win32 `WM_KEYDOWN` and `WM_KEYUP` messages translated by the host input dispatcher are tagged with `InputProvenance::PhysicalWindowsInput`.
- The `InputRouter` queries `WindowManager::get_focused_window()`.
- If a valid focused window exists, the event is pushed directly into that window's event queue.
- If no window has focus, the event is safely dropped with an audit log.

---

## 3. Verification & Evidence

Automated unit testing in `tests/unit/stage222_8_input_provenance_tests.cpp` verified:
1. **Host Coordinate Translation:** Accurate scaling across $1920\times 1080 \to 1284\times 2778$ viewport with border clamping.
2. **Hit-Testing & Focus Switching:** Clicks on overlapping windows correctly hit-tested the top-most window and shifted focus.
3. **Pointer Drag Capture:** Dragging starting inside Window A and moving far into Window B delivered move events solely to Window A until release.
4. **Keyboard Focus Routing:** Key events dispatched directly to the active focused window with `PhysicalWindowsInput` provenance tag intact.

**Result:** 4/4 tests passed (100%).
