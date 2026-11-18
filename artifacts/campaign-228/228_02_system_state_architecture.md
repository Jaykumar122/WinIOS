# WinIOS Campaign 228: Unified System-State Architecture

## 1. Executive Design Overview

Campaign 228 introduces a centralized, unified system-state architecture (`SystemStateManager`) that harmonizes previously decoupled subsystem states into a cohesive desktop runtime.

Rather than having virtual devices, window servers, configuration files, and audio/display components manage disparate, out-of-sync state models, `SystemStateManager` acts as the single authoritative state coordinator for:
- **Appearance & Themes:** Light, Dark, and System automatic theme modes, accent color palettes, and surface contrast.
- **Display Configurations:** Virtual display resolution, scale factors (1.0x, 1.5x, 2.0x), orientation (Portrait/Landscape), and refresh rate metadata.
- **Audio State:** Master volume, mute flags, virtual audio device metadata.
- **Input Preferences:** Key repeat delay/rate, pointer sensitivity, primary mouse button, and pointer visibility.
- **Power & Battery Simulation:** Virtual battery percentage, charging state, low-power mode, screen timeout.
- **Virtual Network Control:** Isolated virtual loopback interface state, adapter enable/disable, traffic telemetry.
- **Task & Window Switcher:** Running process enumeration, active window focus, Z-order activation, lifecycle controls.
- **Accessibility:** Text scaling percentages (100%–200%), high contrast overrides, reduced motion, and focus navigation.
- **User Profiles:** Active virtual user ID (`default_user`, `guest_user`), profile preferences isolation.

---

## 2. Architecture Diagram

```
+---------------------------------------------------------------------------------+
|                                 Compositor / Shell                              |
|   +-----------------------+  +----------------------+  +--------------------+   |
|   |     Control Center    |  |     Task Switcher    |  |    Notification    |   |
|   |  (Quick Toggles/Bars) |  |   (Running Apps Bar) |  |       Center       |   |
|   +-----------+-----------+  +----------+-----------+  +---------+----------+   |
+---------------|-------------------------|------------------------|--------------+
                |                         |                        |
                v                         v                        v
+---------------------------------------------------------------------------------+
|                        SystemStateManager (Authoritative)                       |
|   - Thread-safe state storage (std::recursive_mutex)                           |
|   - State change observer notifications (on_theme_changed, on_display_changed) |
|   - Transactional commit (Commit -> Notify -> Persist)                         |
+---------------------------------------------------------------------------------+
       |               |               |               |               |
       v               v               v               v               v
+-------------+ +-------------+ +-------------+ +-------------+ +-------------+
| Appearance  | |   Display   | |    Audio    | | Task Model  | |Accessiblity |
|   Service   | |   Service   | |   Service   | | & Switcher  | |   Service   |
+-------------+ +-------------+ +-------------+ +-------------+ +-------------+
       |               |               |               |               |
       +---------------+---------------+---------------+---------------+
                                       |
                                       v
                  +-----------------------------------------+
                  |  ConfigurationService / VFS Persistence |
                  |   /System/Library/Preferences/          |
                  |       system_settings.json              |
                  +-----------------------------------------+
```

---

## 3. State Ownership & Invariant Rules

1. **Single Source of Truth:** Subsystem services (e.g. `DisplayService`, `AudioService`, `WindowManager`) read from and report changes to `SystemStateManager`. UI components never manipulate device hardware directly without updating `SystemStateManager`.
2. **Transactional State Updates:** State mutations occur in memory first; validation guards verify bounds (e.g. Volume 0..100, Scale 100..200). If valid, state is committed, observers notified, and persisted atomically.
3. **No UI-Only Illusions:** Control Center and Settings toggles reflect real virtual service state. If Low Power Mode is enabled in Control Center, `SystemStateManager::is_low_power_mode()` reports `true` to all services.
4. **Honest Simulation Boundaries:** Network is explicitly documented as isolated loopback; battery and power state are software simulations with no host hardware penetration.
