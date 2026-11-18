# WinIOS Campaign 223 — Window Interaction & Pointer Gestures (Stage 223.10)

## 1. Executive Summary

Stage 223.10 verifies window decorations, non-client hit-testing, dragging/movement, resizing constraints, and multi-window Z-order management.

---

## 2. Non-Client Hit Testing & Window Controls

The `WindowManager` provides fine-grained non-client geometry partitioning:
- **Title Bar**: `[frame.x, frame.y, frame.width, kTitleBarHeight (28px)]`
- **Close Button**: Top-right corner within title bar (`[frame.x + width - 24, frame.y + 5, 18, 18]`)
- **Minimize Button**: Adjacent to close button
- **Client Frame**: Inset by 1px border on all sides and 28px title bar top offset

---

## 3. Window Operations & Security

1. **Authorization**: `move_window`, `resize_window`, `set_window_state`, and `destroy_window` verify that `caller_pid` matches the window's `owner_pid` (or system kernel PID 0). Attempts by foreign processes fail deterministically.
2. **Dimension Clamping**: Window resizing enforces minimum dimensions (`min_width = 100`, `min_height = 80`) and maximum display boundaries.
3. **Z-Order Stacking**: Clicking inside any window elevates it to the top of the Z-stack (`raise_to_top`), updates `focused_window_id`, and assigns keyboard/pointer focus.

---

## 4. Test Verification

`tests/unit/stage223_10_window_interaction_tests.cpp` (100% PASS):
- Non-client hit-testing for title bar, close button, client frame, and outer bounds.
- Window dragging with client frame translation and process authorization checks.
- Overlapping window Z-order elevation and top-level hit detection.
- Window closing and descriptor deallocation.
