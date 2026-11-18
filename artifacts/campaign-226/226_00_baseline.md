# WinIOS Campaign 226 — Stage 226.0 Baseline Verification & Subsystem Inventory

## 1. Baseline Integrity Verification
- **Repository Root:** `D:\WinISO26\WinIOS\`
- **Branch:** `campaign-191r`
- **Required Baseline Commit:** `4169eb54a81b6ad3875e9291eca2cd1193242c74`
- **Actual HEAD Commit:** `4169eb54a81b6ad3875e9291eca2cd1193242c74` (MATCH)
- **Git Worktree Status:** Clean (no uncommitted modifications)
- **Target Research Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), ARM64 EL0, 16 KB translation granule
- **Public XNU Reference:** `xnu-10002.1.13` (pinned public Darwin EL0 ABI baseline)

---

## 2. Protected Invariant Assets Verification

| Invariant Path | Expected State / SHA-256 | Actual State / SHA-256 | Status |
| :--- | :--- | :--- | :---: |
| `D:\WinISO26\WinISO26_README.md` | Nonexistent (`False`) | Nonexistent (`False`) | **MATCH** |
| `D:\WinISO26\WinIOS_README.md` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | **MATCH** |
| `D:\WinISO26\WinIOS\docs\progress.md` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | **MATCH** |
| `D:\WinISO26\Inferno-Research\` | Exists and untouched (`True`) | Exists and untouched (`True`) | **MATCH** |

---

## 3. Toolchain & Build Configuration

- **Host OS:** Windows 11 Pro x64 (Build 26100+)
- **Host Compiler:** Visual Studio 2026 / MSVC C++20 (`/W4 /WX /permissive-`)
- **CMake Version:** 4.4.4
- **CTest Version:** 4.4.4
- **Guest Cross-Compiler:** Zig 0.17.0 (`zig cc -target aarch64-macos -nostdlib -fno-builtin -fno-sanitize=all -O2`)
- **Python Runtime:** Python 3.12.10

---

## 4. Test Discovery & Baseline Test Results

- **Total Test Discovery Count:** **849 tests** (from `#1: memory_tests` through `#849: stage225_23_adversarial_tests`).
- **Baseline Test Suite Execution (Campaign 225 Suite):**
  - Command: `ctest --test-dir D:\WinISO26\WinIOS\build -C RelWithDebInfo -R "stage225_" --output-on-failure`
  - Total Tests: 13
  - Passed: 13 (100%)
  - Failed: 0
  - Execution Time: 0.91 sec

---

## 5. Campaign 225 Subsystem & API Inventory

### Category C Syscalls (546–560)
- `SYS_WINIOS_DEVICE_ENUM` (546): Enumerates virtual devices matching class filter.
- `SYS_WINIOS_DISPLAY_GET_CONFIG` (547): Queries display geometry, refresh rate, and scale.
- `SYS_WINIOS_AUDIO_GET_STATUS` (548): Queries PCM audio output state and queue usage.
- `SYS_WINIOS_AUDIO_SUBMIT_BUFFER` (549): Submits linear PCM audio data (64 KB single limit).
- `SYS_WINIOS_SENSOR_SAMPLE` (550): Polls 3-axis accelerometer, gyroscope, or ambient light.
- `SYS_WINIOS_SENSOR_SUBSCRIBE` (551): Subscribes to sensor updates with interval bounds [10ms, 1000ms].
- `SYS_WINIOS_TIME_GET` (552): Queries monotonic steady clock and UTC timespec.
- `SYS_WINIOS_TIMER_SET` (553): Schedules high-resolution one-shot or periodic virtual timers.
- `SYS_WINIOS_NET_SEND` (554): Transmits Ethernet frame into virtual network (MTU 1514).
- `SYS_WINIOS_NET_RECV` (555): Receives Ethernet frame from loopback queue.
- `SYS_WINIOS_NET_STATUS` (556): Queries virtual network adapter statistics.
- `SYS_WINIOS_NOTIFICATION_POST` (557): Posts system toast banner into ring buffer.
- `SYS_WINIOS_NOTIFICATION_DISMISS` (558): Dismisses notification by ID.
- `SYS_WINIOS_DEVICE_CONTROL` (559): Unified device IOCTL interface.
- `SYS_WINIOS_DEVICE_DIAG_SUMMARY` (560): Queries aggregate virtual hardware diagnostics.

### Device Infrastructure
- `DeviceRegistry`: Thread-safe singleton managing generation-tagged handles (`(gen << 32) | id`).
- `IVirtualDevice` / `VirtualDeviceBase`: Common lifecycle state machine (`Uninitialized` $\rightarrow$ `Ready` $\rightarrow$ `Running` $\rightarrow$ `Suspended` $\rightarrow$ `Stopped` $\rightarrow$ `Error` $\rightarrow$ `Removed`).
- Subsystems: `VirtualDisplayService`, `VirtualPointerDevice`, `VirtualKeyboardDevice`, `VirtualAudioService`, `VirtualSensorService`, `VirtualNetworkService`, `VirtualClockService`, `VirtualLifecycleService`, `VirtualNotificationService`.

### Security & Capabilities
- Hardware capabilities: `DeviceEnumerate` (`1<<14`), `DeviceInput` (`1<<15`), `AudioOutput` (`1<<16`), `AudioInput` (`1<<17`), `SensorAccess` (`1<<18`), `NetworkAccess` (`1<<19`), `NotificationPost` (`1<<20`).

### Execution Runtime & Guest SDK
- SDK headers: `guest-sdk/include/winios/devices/device.h`, `winios.h`, `winios_types.h`.
- C runtime: `guest-sdk/lib/winios_runtime.c` hardened with `volatile` memory operations and `-fno-builtin`.
- Embedded workloads: `campaign225_device_center` and `campaign225_system_monitor`.
