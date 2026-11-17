# WinIOS Campaign 213 — Guest UI Runtime & Syscall API Reference

## 1. Provenance and Design Intent

WinIOS Category C system calls provide explicit, documented guest-to-host interfaces for graphical display management and input event reception.

All interfaces maintain strict separation from Darwin/BSD compatibility calls:
- Syscall numbers are explicitly allocated in the Category C range ($500 \dots 506$).
- Parameter validation verifies guest pointers using `memory::UserAddressSpace::copyin` and `copyout`.
- Faulty or out-of-bounds pointers yield `EFAULT` (14).
- Invalid window identifiers yield `EBADF` (9).
- Permission violations (e.g. cross-process surface tampering) yield `EPERM` (1).
- Invalid dimensions yield `EINVAL` (22).

## 2. Category C Display Syscalls Reference

### `SYS_winios_window_create` (Syscall 500)
Creates a process-owned window and associates an underlying `DisplaySurface`.
- **Calling Convention (DarwinStrict)**:
  - `X16`: `500`
  - `X0`: `title_ptr` (Guest virtual address of UTF-8 string)
  - `X1`: `title_len` (Length of title string)
  - `X2`: `x` (Signed 32-bit window origin X)
  - `X3`: `y` (Signed 32-bit window origin Y)
  - `X4`: `width` (32-bit window width)
  - `X5`: `height` (32-bit window height)
- **Returns**: Window ID ($> 0$) on success (`PSTATE.C = 0`), or errno on failure (`PSTATE.C = 1`).

### `SYS_winios_window_destroy` (Syscall 501)
Destroys a window, unmaps its surface, reclaims memory quotas, and transfers focus.
- **Calling Convention**:
  - `X16`: `501`
  - `X0`: `window_id`
- **Returns**: `0` on success.

### `SYS_winios_surface_update` (Syscall 502)
Transfers an array of raw pixel bytes from guest user memory into the surface backing store.
- **Calling Convention**:
  - `X16`: `502`
  - `X0`: `window_id`
  - `X1`: `buffer_va` (Guest virtual address)
  - `X2`: `buffer_size` (Must equal surface byte count)
- **Returns**: `0` on success.

### `SYS_winios_surface_present` (Syscall 503)
Flags the surface as fully damaged and invokes the Compositor redraw pass.
- **Calling Convention**:
  - `X16`: `503`
  - `X0`: `window_id`
- **Returns**: `0` on success.

### `SYS_winios_poll_event` (Syscall 504)
Polls the calling process's input queue for the next pending user interaction.
- **Calling Convention**:
  - `X16`: `504`
  - `X0`: `event_va` (Destination pointer to 32-byte `GuestInputEvent` buffer)
  - `X1`: `event_size` (Must be $\ge 32$)
- **Returns**:
  - `1`: An event was copied into the guest buffer.
  - `0`: Queue is currently empty.
  - Negative/Errno: Invalid memory pointer or undersized buffer.

### `SYS_winios_surface_fill` (Syscall 505)
Hardware-accelerated rectangle fill directly within the window's surface.
- **Calling Convention**:
  - `X16`: `505`
  - `X0`: `window_id`
  - `X1`: `x` (Rectangle origin X)
  - `X2`: `y` (Rectangle origin Y)
  - `X3`: `width` (Rectangle width)
  - `X4`: `height` (Rectangle height)
  - `X5`: `color` (32-bit ARGB/BGRA color value)
- **Returns**: `0` on success.

### `SYS_winios_window_set_title` (Syscall 506)
Updates the window metadata title string.
- **Calling Convention**:
  - `X16`: `506`
  - `X0`: `window_id`
  - `X1`: `title_ptr`
  - `X2`: `title_len`
- **Returns**: `0` on success.

## 3. Typography and Permissive Font Provenance

To guarantee 100% legal compliance and repository hygiene:
- No Apple proprietary fonts (e.g. San Francisco, New York) or icons are bundled.
- WinIOS embeds a clean 760-byte 8x8 bitmap font table covering ASCII 32 (` `) through ASCII 126 (`~`).
- Glyphs support arbitrary integer scaling ($1\times, 2\times, 3\times$) with bilinear fill for sharp, legibility across Super Retina resolutions.
