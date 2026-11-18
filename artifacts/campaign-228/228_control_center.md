# WinIOS Campaign 228: Control Center Quick-Access Architecture

## 1. Overview & Purpose

The WinIOS Control Center (`src/settings/control_center_service.h`, `.cpp`) provides an interactive desktop quick-access card allowing the user or guest applications to inspect and modify platform state without opening full preference dialogs.

---

## 2. Geometry & Placement

- **Panel Dimensions:** Fixed bounded card of 320x360 pixels.
- **Anchor Position:** Top-right desktop quadrant (default `x = 460`, `y = 32`), floating over regular window surfaces and under modal system alerts.
- **Boundary Clamping:** Guaranteed minimum size of 100x100 pixels; never clips outside the compositor view boundaries.

---

## 3. Interactive Behaviors

### 3.1 Pointer Routing & Outside-Click Dismissal
- When visible, pointer clicks within the card `[460, 32, 780, 392]` are consumed by the Control Center card.
- Pointer clicks outside the card automatically dismiss the Control Center (`visible_ = false`) and return `true` indicating the click was consumed as a dismissal action.

### 3.2 Keyboard Dismissal
- Pressing `Escape` (key code 27) dismisses the open Control Center immediately.

### 3.3 Quick Toggles & Real Service Synchronization
All toggles call into `SystemStateManager` mutators directly:
- `toggle_theme()`: Flips `ThemeMode::Dark` <-> `ThemeMode::Light`.
- `toggle_accent()`: Cycles through the 8 accent colors (Blue -> Purple -> Pink -> Red -> Orange -> Yellow -> Green -> Graphite).
- `cycle_display_scale()`: Cycles 100% -> 125% -> 150% -> 200% -> 100%.
- `toggle_audio_mute()`: Toggles virtual audio output mute state.
- `step_volume_up(step)` / `step_volume_down(step)`: Adjusts volume in increments of 10% (bounded 0..100%).
- `toggle_low_power_mode()`: Toggles power-saving mode.
- `toggle_network_adapter()`: Toggles virtual loopback adapter enablement.
- `toggle_do_not_disturb()`: Toggles notification suppression.
