# WinIOS Campaign 225 — Stage 225.21 Native ARM64 Guest Applications

## 1. Workload Overview
Campaign 225 introduces two native ARM64 guest applications compiled directly from C source code to genuine ARM64 Mach-O binaries using Zig 0.17.0 (`zig cc -target aarch64-macos -nostdlib -fno-builtin -fno-sanitize=all -O2`):
1. **Device Center (`campaign225_device_center.c`):** Tests device enumeration, display configuration, audio service status, multi-axis sensors, private storage persistence, and UI rendering.
2. **System Monitor (`campaign225_system_monitor.c`):** Tests monotonic and UTC clock queries, virtual network loopback packet echoing, system notification posting, and device diagnostic summaries.

---

## 2. Device Center (`campaign225_device_center.c`)
- **Binary Size:** 51,992 bytes (embedded in `embedded_campaign225_workloads.inl`).
- **Target PID / ASID:** PID 91, ASID 9.
- **Workflow:**
  1. Creates application window ($420 \times 520$) with blue titlebar.
  2. Enumerates virtual devices via `winios_device_enum` (validates $\ge 9$ devices).
  3. Queries display configuration via `winios_display_get_config`.
  4. Inspects audio subsystem via `winios_audio_get_status`.
  5. Samples accelerometer and ambient light sensor via `winios_sensor_sample`.
  6. Writes persistence verification profile `devcenter.cfg` to private isolated storage.
  7. Presents frame buffer via `winios_surface_present`.
  8. Exits cleanly with exit code `91`.
- **Execution Performance:** Retires $46,857$ genuine ARM64 guest instructions in EL0.

---

## 3. System Monitor (`campaign225_system_monitor.c`)
- **Binary Size:** 51,960 bytes (embedded in `embedded_campaign225_workloads.inl`).
- **Target PID / ASID:** PID 92, ASID 10.
- **Workflow:**
  1. Creates dark-themed system monitor window ($400 \times 480$).
  2. Queries monotonic steady clock and UTC timespec via `winios_time_get`.
  3. Transmits `"WINIOS_NET_ECHO"` packet (16 bytes) via `winios_net_send`.
  4. Receives loopback echo via `winios_net_recv` and verifies identical payload.
  5. Posts system toast alert `"Services operational"` via `winios_notification_post`.
  6. Queries device telemetry summary via `winios_device_diag_summary`.
  7. Presents frame buffer via `winios_surface_present`.
  8. Exits cleanly with exit code `92`.
- **Execution Performance:** Retires $13,296$ genuine ARM64 guest instructions in EL0.

---

## 4. Multi-App Integration (Stage 225.22)
In `stage225_22_multi_app_device_tests.cpp`, both applications execute concurrently under the round-robin/priority scheduler in isolated address spaces:
- PID 91 (Device Center) and PID 92 (System Monitor) run side-by-side.
- Both processes terminate with their expected exit codes (91 and 92).
- Zero cross-process memory corruption or capability leakage observed.
- Total combined retired instructions: $> 55,000$ genuine ARM64 instructions.
