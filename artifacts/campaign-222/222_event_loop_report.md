# Campaign 222 — Stage 222.7 Event Queue & Event Loop v2 Report

## Objectives & Achievements
Stage 222.7 upgrades the WinIOS event delivery pipeline to Event Loop v2, supporting mouse buttons, pointer motion, wheel delta, key modifier bitmasks, window-relative coordinate mapping, auditable hardware provenance, and backwards compatibility with legacy guest event structures.

## Technical Specifications

### 1. `winios_event_v2_t` Layout & Alignment
- **Size**: Exactly 64 bytes, 8-byte aligned.
- **Fields**:
  - `type`: `winios_event_v2_type_t` (e.g. `WINIOS_EVENT_V2_MOUSE_DOWN`, `WINIOS_EVENT_V2_MOUSE_MOVE`, `WINIOS_EVENT_V2_MOUSE_UP`, `WINIOS_EVENT_V2_MOUSE_WHEEL`, `WINIOS_EVENT_V2_WINDOW_RESIZE`).
  - `contact_id`: Multi-touch slot or mouse button ID (`1 = Left, 2 = Right, 3 = Middle`).
  - `x`, `y`: Window-relative coordinates (adjusted for target window frame position).
  - `key_code`: Virtual key or character code.
  - `modifiers`: Bitmask (`Shift=0x01, Ctrl=0x02, Alt=0x04, Meta=0x08, CapsLock=0x10`).
  - `wheel_delta`: Signed integer scrolling delta (+120 for forward/up, -120 for backward/down).
  - `window_id`: ID of the destination window.
  - `timestamp_us`: Microsecond timestamp.
  - `provenance`: 24-byte `winios_input_provenance_t` metadata record.

### 2. Dual-Mode Syscall Delivery (`kSysWiniosPollEvent`)
- The host dispatcher inspects the buffer size passed in `arg1`:
  - If `size >= sizeof(winios_event_v2_t)` (64 bytes): Copies out full `winios_event_v2_t`.
  - If `size == sizeof(winios_event_t)` (32 bytes): Copies out legacy `GuestInputEvent` structure.
- Non-blocking poll returns `1` when an event is dequeued and `0` when the per-process queue is empty.

### 3. Verification
- Built and ran `stage222_7_event_queue_tests.exe`: 100% pass across struct packing, V2 event delivery, wheel delta, provenance auditing, and backward compatibility.
