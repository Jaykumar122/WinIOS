# WinIOS Campaign 225 — Stage 225.17 Virtual Power & Lifecycle Service

## 1. Power Service Overview
The `VirtualLifecycleService` ([src/devices/lifecycle_service.h](file:///D:/WinISO26/WinIOS/src/devices/lifecycle_service.h), [src/devices/lifecycle_service.cpp](file:///D:/WinISO26/WinIOS/src/devices/lifecycle_service.cpp)) emulates Apple Silicon device power management, battery state reporting, thermal throttling thresholds, and system power transitions.

---

## 2. Power States & Telemetry

### Power State Enumeration:
- `PowerState::Active`: Normal operating mode; all virtual hardware clocked at standard frequencies.
- `PowerState::LowPower`: Low power mode active; sensor polling throttled, display refresh reduced to 30 Hz.
- `PowerState::Suspended`: System sleep; CPU execution halted, timers paused.
- `PowerState::Critical`: Battery level below $5\%$; urgent application state serialization triggered.

### Battery Telemetry Struct:
```cpp
struct BatteryInfo {
    uint32_t state;             // 0: Unknown, 1: Discharging, 2: Charging, 3: Full
    uint32_t level_percent;     // [0, 100]
    uint32_t time_remaining_min;// Estimated remaining minutes on battery
    bool     ac_connected;      // True if virtual charger connected
    float    temperature_c;     // Emulated battery temperature in Celsius
};
```

---

## 3. Power Event Broadcasting
The lifecycle service broadcasts state changes to registered platform observers and window server instances:
- On transition to `LowPower`, display and audio services scale down resource consumption.
- On AC adapter plug/unplug, power state notifications are broadcasted to active guest applications.
- Guest apps query power state via unified device control IOCTLs on `DeviceClass::Power`.

---

## 4. Verification Evidence
- [tests/unit/stage225_3_device_registry_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_3_device_registry_tests.cpp): Validates device registration, power telemetry querying, and state transition dispatching.
