# WinIOS Campaign 225 — Stage 225.6 & 225.7 Virtual Display Service & Configuration

## 1. Display Service Overview
The `VirtualDisplayService` ([src/devices/display_service.h](file:///D:/WinISO26/WinIOS/src/devices/display_service.h), [src/devices/display_service.cpp](file:///D:/WinISO26/WinIOS/src/devices/display_service.cpp)) provides hardware-abstracted screen geometry, scaling, refresh rate, and resolution switching to guest applications. It interfaces directly with the platform's `VirtualDisplay` composition engine.

---

## 2. Display Parameters & Validation
The service enforces strict bounds conforming to Apple mobile and desktop display profiles (including the iPhone 15 Pro Max profile: $1290 \times 2796$ @ 60 Hz with $3.0\times$ display scale).

### Display Configuration Struct (`winios_display_config_t`)
```c
typedef struct {
    uint32_t width;             // Pixel width: [320, 3840]
    uint32_t height;            // Pixel height: [240, 3840]
    uint32_t refresh_rate;      // Refresh rate (default 60 Hz)
    float    scale;             // Coordinate scale factor: [0.5, 4.0]
    uint32_t format;            // Pixel format (0: ARGB8888, 1: RGBA8888, 2: RGB565)
    uint32_t flags;             // Mode flags (bit 0: fullscreen, bit 1: HDR, bit 2: VRR)
} winios_display_config_t;
```

### Validation Constraints
- Width must be within $[320, 3840]$.
- Height must be within $[240, 3840]$ (accommodating 4K portrait displays up to $3840$ vertical pixels).
- Scale factor must be within $[0.5, 4.0]$.
- Refresh rate must be non-zero (nominally 60 Hz).

---

## 3. Syscall Dispatch & Reconfiguration
Guest applications query display parameters via Syscall 547 (`SYS_WINIOS_DISPLAY_GET_CONFIG`):
```c
int32_t winios_display_get_config(winios_display_config_t* out_config);
```
- Reconfiguration (`set_config`) validates bounds before triggering `VirtualDisplay::reconfigure()`.
- On resolution change, the service notifies registered resize callbacks, allowing the `WindowManager` and active guest applications to resize root surfaces deterministically.

---

## 4. Verification Evidence
- [tests/unit/stage225_6_display_service_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_6_display_service_tests.cpp): Validates default configuration retrieval, resolution switching (including $1290 \times 2796$), scale validation, invalid bounds rejection, and resize notifications.
