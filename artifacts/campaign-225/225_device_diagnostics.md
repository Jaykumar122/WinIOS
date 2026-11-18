# WinIOS Campaign 225 — Stage 225.19 Virtual Device Diagnostics & Telemetry

## 1. Diagnostics Subsystem Integration
The `DiagnosticsService` ([src/services/diagnostics_service.h](file:///D:/WinISO26/WinIOS/src/services/diagnostics_service.h), [src/services/diagnostics_service.cpp](file:///D:/WinISO26/WinIOS/src/services/diagnostics_service.cpp)) aggregates real-time hardware status, fault records, and performance metrics across all registered virtual devices.

---

## 2. Diagnostics Telemetry Descriptor (`winios_device_diag_t`)
Guest diagnostic applications retrieve system health via Syscall 560 (`SYS_WINIOS_DEVICE_DIAG_SUMMARY`):

```c
typedef struct {
    uint32_t total_devices;         // Total registered virtual devices (e.g. 9)
    uint32_t active_devices;        // Total devices in DeviceState::Running
    uint32_t fault_count;           // Cumulative fault events across all devices
    uint32_t audio_underruns;       // Audio subsystem underruns encountered
    uint32_t network_drops;         // Packets dropped due to queue congestion
    uint32_t active_timers;         // Current active virtual timers
    uint32_t active_notifications;  // Current active notification banners
    uint32_t reserved[9];           // Reserved alignment padding
} winios_device_diag_t;
```

---

## 3. Kernel Dispatch & Safety Enforcement
- Handled in `UserSyscallDispatcher`:
  1. Validates that calling application possesses `DeviceEnumerate` or administrative telemetry capability.
  2. Queries `DeviceRegistry::get().get_device_summary()`.
  3. Populates `winios_device_diag_t` structure.
  4. Copies structure to user memory using `uas.copyout()` with checked boundary bounds.

---

## 4. Verification Evidence
- [tests/unit/stage225_21_system_monitor_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_21_system_monitor_tests.cpp): Validates telemetry retrieval from native ARM64 guest code and verifies accurate device counts.
