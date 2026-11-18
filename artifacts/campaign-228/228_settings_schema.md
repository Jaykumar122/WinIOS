# WinIOS Campaign 228: System Settings Schema & Persistence Specification

## 1. Architecture Overview

Campaign 228 establishes a centralized, typed, transaction-safe system state management subsystem located at `src/settings/`:
- `settings_types.h`: Definitive data models for appearance, display, audio, input, power, network, accessibility, and user profiles.
- `settings_schema.h` & `settings_schema.cpp`: Versioned schema validation, typed bounds enforcement, bounded serialization (max 32 KB), and default factories.
- `system_state_manager.h` & `system_state_manager.cpp`: Transactional mutation engine with atomic rollback, recursive mutex thread safety, observer event dispatch (`SystemStateObserver`), and VFS persistence to `/System/Library/Preferences/system_settings.ini`.

---

## 2. Typed Schema Definitions

### 2.1 Appearance Settings
- **ThemeMode:** `Light` (0), `Dark` (1), `System` (2). Default: `Dark`.
- **AccentColor:** `Blue` (0), `Purple` (1), `Pink` (2), `Red` (3), `Orange` (4), `Yellow` (5), `Green` (6), `Graphite` (7). Default: `Blue`.
- **High Contrast:** Boolean flag enabling pure-contrast palette overrides (pure black `#000000` / pure white `#FFFFFF`). Default: `false`.
- **Reduce Transparency:** Boolean flag disabling alpha blending on translucent surfaces. Default: `false`.

### 2.2 Display Settings
- **Resolution:** Clamped between 320x240 and 3840x2160. Default: 800x600.
- **DisplayScale:** `Scale100` (100%), `Scale125` (125%), `Scale150` (150%), `Scale200` (200%). Default: `Scale100`.
- **DisplayOrientation:** `Landscape` (0), `Portrait` (1). Default: `Landscape`.
- **Refresh Rate:** 30 Hz to 240 Hz. Default: 60 Hz.

### 2.3 Audio Settings
- **Master Volume:** Clamped 0% to 100%. Default: 80%.
- **Is Muted:** Boolean mute state. Default: `false`.
- **Output Device:** Virtual device description string. Default: `"Virtual Speakers (Stereo)"`.

### 2.4 Power Settings (Virtual Simulation)
- **Battery Level:** 0% to 100%. Default: 100%.
- **Is Charging:** Boolean. Default: `true`.
- **Low Power Mode:** Simulated throttle / power-saving flag. Default: `false`.
- **Screen Idle Timeout:** 30s to 3600s. Default: 300s.

### 2.5 Network Settings (Virtual Loopback)
- **Adapter Enabled:** Boolean interface power state. Default: `true`.
- **Loopback Only:** Hardcoded `true` to ensure absolute research isolation with zero host socket or internet access.

### 2.6 Accessibility Settings
- **Text Scale Percent:** Bounded between 100% and 200%. Default: 100%.
- **High Contrast:** High contrast override flag. Default: `false`.
- **Reduce Motion:** Animation suppression flag. Default: `false`.
- **Focus Indicator Visible:** High-visibility bounding box indicator. Default: `true`.

---

## 3. Transaction Model & Rollback Guarantees

Mutations to system settings are executed through the `update_settings(mutator)` transactional method:
1. Lock acquired (`std::recursive_mutex lock_`).
2. Snapshot of existing state `prev = settings_` created.
3. User mutator invoked against `curr = settings_`.
4. Schema validation `SettingsSchema::validate_system_settings(curr)` executed.
5. If validation fails:
   - State immediately restored from `prev`.
   - Observers are NOT notified.
   - Returns `false`.
6. If validation passes:
   - State committed.
   - Observer callbacks dispatched outside critical mutation.
   - Auto-persisted to VFS.
   - Returns `true`.
