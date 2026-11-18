# WinIOS Application Window and Input SDK Report

**Campaign:** 221 (Stage 221.13)  
**Subsystem:** Window Management, Input Routing & Focus Controls  

---

## 1. Application-Facing Window APIs

Declared in `<winios/ui/window.h>`:
- `winios_window_create(title, x, y, width, height)`: Creates a new top-level application window on the virtual desktop. Returns unique `winios_wid_t`.
- `winios_window_destroy(wid)`: Closes and releases window resources.
- `winios_window_set_title(wid, title)`: Updates title bar text dynamically.
- `winios_window_get_geometry(wid, geom)`: Queries position, size, and focus state.
- `winios_window_set_focus(wid)`: Requests keyboard and pointer focus.

---

## 2. Input Isolation & Security

1. **Window Ownership:** A process may only operate on windows created by itself or its child processes. Attempts by Process A to destroy or manipulate Process B's window fail with `kErrnoEBADF`.
2. **Input Targeting:** Keystroke and pointer events are strictly routed to the focused window of the focused process, preventing background eavesdropping.
