# WinIOS Campaign 215 — Developer SDK & Category C ABI v1.0 Reference

## 1. Overview

The WinIOS Developer SDK (`include/winios_sdk/winios_sdk.h`) provides a clean, C-compatible interface for authoring freestanding ARM64 guest applications targeted for the WinIOS runtime.

---

## 2. Category C ABI v1.0 Syscall Numbers

Category C syscalls occupy the designated extension range 500 through 507:

| Syscall Number | Constant Identifier | Arguments | Return Value | Description |
| :---: | :--- | :--- | :---: | :--- |
| **500** | `kSysWiniosWindowCreate` | `X0`: x, `X1`: y, `X2`: width, `X3`: height | WID (>0) or -errno | Create top-level application window |
| **501** | `kSysWiniosWindowDestroy`| `X0`: window_id | 0 or -errno | Destroy application window |
| **502** | `kSysWiniosSurfaceUpdate`| `X0`: window_id, `X1`: buf_va, `X2`: size | 0 or -errno | Present ARGB pixel buffer to window |
| **503** | `kSysWiniosSurfaceCopy`  | `X0`: dst_wid, `X1`: src_wid | 0 or -errno | Copy surface contents across windows |
| **504** | `kSysWiniosPollEvent`    | `X0`: evt_va, `X1`: evt_buf_size | 0, 1 (has event), or -errno | Poll next queued input event |
| **505** | `kSysWiniosSurfaceFill`  | `X0`: window_id, `X1`: rect_va, `X2`: color | 0 or -errno | Fill rectangle with 32-bit ARGB color |
| **506** | `kSysWiniosSetWindowTitle`| `X0`: window_id, `X1`: title_va, `X2`: len | 0 or -errno | Set window title in Window Server |
| **507** | `kSysWiniosGetAbiVersion`| None | `0x00010000` | Query Category C ABI version (v1.0) |

*Note: Syscalls >= 508 are unassigned and return `kErrnoENOSYS` (78).*

---

## 3. ABI Conventions

### DarwinStrict Mode
- **Syscall Number**: Loaded into register **`X16`**.
- **Arguments**: Passed in registers **`X0` through `X5`**.
- **Success Return**: Register `X0` contains return value; `PSTATE.C` (Carry flag) is cleared (0).
- **Error Return**: Register `X0` contains positive errno (e.g. `14` for `EFAULT`); `PSTATE.C` is set (1).

### WinIOSFast Mode
- **Syscall Number**: Loaded into register **`X0`**.
- **Arguments**: Passed in registers **`X1` through `X6`**.
- **Success Return**: Register `X0` contains non-negative return value.
- **Error Return**: Register `X0` contains negative errno (e.g. `-14`).

---

## 4. Key Data Types

```c
typedef uint32_t winios_wid_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} winios_rect_t;

typedef enum {
    WINIOS_EVENT_NONE        = 0,
    WINIOS_EVENT_TOUCH_DOWN  = 1,
    WINIOS_EVENT_TOUCH_UP    = 2,
    WINIOS_EVENT_TOUCH_MOVE  = 3,
    WINIOS_EVENT_KEY_DOWN    = 4,
    WINIOS_EVENT_KEY_UP      = 5,
    WINIOS_EVENT_CHAR        = 6,
    WINIOS_EVENT_WINDOW_BLUR = 7,
    WINIOS_EVENT_WINDOW_FOCUS= 8
} winios_event_type_t;

typedef struct {
    uint32_t type;       // winios_event_type_t
    uint32_t wid;        // Window ID
    int32_t  x;          // Window-relative X coordinate
    int32_t  y;          // Window-relative Y coordinate
    uint32_t key_char;   // Character / Virtual Key code
    uint32_t flags;      // Modifiers (Shift, Ctrl, etc.)
    uint64_t timestamp;  // Event timestamp in microseconds
} winios_event_t;
```
