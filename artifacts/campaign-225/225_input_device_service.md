# WinIOS Campaign 225 — Stage 225.8 & 225.9 Input Device Services (Pointer & Keyboard)

## 1. Virtual Input Subsystem Overview
Input handling in WinIOS is managed through dedicated virtual devices:
1. `VirtualPointerDevice` ([src/devices/input_device_service.h](file:///D:/WinISO26/WinIOS/src/devices/input_device_service.h)): Tracks relative/absolute coordinates, button bitmasks (Left, Right, Middle), scroll wheel delta, and active window hover targeting.
2. `VirtualKeyboardDevice`: Manages a full 256-entry virtual key matrix, tracking press states, modifiers (Shift, Control, Alt, Command/Meta), and keyboard focus targets.

---

## 2. Pointer Tracking & Bounds Clamping
The pointer device tracks coordinates relative to the active display resolution:
- All coordinate updates clamp $X \in [0, \text{display\_width}-1]$ and $Y \in [0, \text{display\_height}-1]$.
- Position delta ($\Delta X, \Delta Y$) is maintained for relative mouse input.
- Button masks support multi-button mouse state tracking:
  - Bit 0: Left Button
  - Bit 1: Right Button
  - Bit 2: Middle Button
- Focus management: Tracks target window ID (`focus_window_id_`). When focus changes or the device stops, `release_all_buttons()` is automatically invoked to prevent stuck mouse drag states.

---

## 3. Keyboard Matrix & Modifier State
The keyboard device maintains:
- `key_down_` array: Boolean state for all 256 standard keycodes.
- `modifiers_` bitmask:
  - Bit 0: Shift (`0x01`)
  - Bit 1: Control (`0x02`)
  - Bit 2: Alt / Option (`0x04`)
  - Bit 3: Meta / Command (`0x08`)
- Defensive release: `release_all_keys()` clears all active pressed keys and modifier flags on window blur, process termination, or device suspension.

---

## 4. Verification Evidence
- [tests/unit/stage225_8_input_device_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_8_input_device_tests.cpp): Validates pointer motion clamping, button transitions, wheel delta accumulation, keypress tracking, modifier calculations, and clean key release on focus change.
