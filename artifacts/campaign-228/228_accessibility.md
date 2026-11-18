# WinIOS Campaign 228: Accessibility Architecture & Services

## 1. Overview

Campaign 228 introduces the WinIOS Accessibility Subsystem (`src/settings/accessibility_service.h`, `.cpp`) providing keyboard focus navigation, semantic role identification, dynamic text scaling, and high-contrast styling across all guest user interfaces.

---

## 2. Accessibility Node Hierarchy

Visual elements register semantic metadata independently of pixel drawing operations:
- **Roles (`AccessibilityRole`):**
  - `Button` (0)
  - `StaticText` (1)
  - `Slider` (2)
  - `Checkbox` (3)
  - `Window` (4)
  - `Dialog` (5)
  - `Menu` (6)
  - `ListItem` (7)
- **Node Attributes:**
  - `id`: Unique 64-bit element identifier.
  - `label`: Screen reader accessible text name.
  - `value`: Current state / value string (e.g. "80%", "Checked").
  - `bounds`: Screen rectangle `[x, y, width, height]`.
  - `is_focused`: Boolean keyboard focus state.
  - `is_enabled`: Interaction eligibility flag.

---

## 3. Focus Traversal & Keyboard Navigation

- **Sequential Traversal:** `focus_next()` and `focus_prev()` navigate circularly through registered nodes.
- **Explicit Focus:** `set_focus(id)` directs focus to target element.
- **Action Invocation:** `activate_focused()` invokes registered action handler (e.g. clicking a focused button via Space/Enter key).

---

## 4. Visual Adaptations

- **Text Scaling:** Configurable text scaling factor from 1.0x (100%) to 2.0x (200%), applied to font glyph rasters.
- **High-Contrast Theme:** Replaces subtle gradients with pure black backgrounds (`#000000`), pure white text (`#FFFFFF`), and high-visibility yellow focus indicators (`#FFFF00`).
- **Reduce Motion:** Suppresses transition animations in window animator and task switcher.
