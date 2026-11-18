# WinIOS Campaign 225 — Test Results & Test Matrix

## 1. Executive Test Summary
- **Total Campaign 225 Unit & Integration Tests:** 13 targets
- **Total CTest Invocations:** 13 passed / 0 failed (100% pass rate)
- **Total Regression Suite (Campaigns 223, 224, 225):** 38 passed / 0 failed (100% pass rate)
- **Compiler Warnings / Errors:** 0 (`/W4 /WX /permissive-`)
- **Host Platform:** Windows 11 x64, Visual Studio 2026, MSVC C++20, CMake 3.31.6

---

## 2. Campaign 225 Test Suite Breakdown

| Test ID | Target Name | Scope / Functionality Tested | Execution Time | Status |
| :---: | :--- | :--- | :---: | :---: |
| `837` | `stage225_3_device_registry_tests` | Registry registration, ID allocation, 64-bit generation handles, ABA defense, lifecycle broadcasting | 0.04s | **PASSED** |
| `838` | `stage225_5_device_capability_tests` | Capability manager device masks (`1<<14` to `1<<20`), PID authorization checks, permission denials | 0.04s | **PASSED** |
| `839` | `stage225_6_display_service_tests` | Display config retrieval, resolution switching (up to 1290x2796), scale limits, resize callbacks | 0.03s | **PASSED** |
| `840` | `stage225_8_input_device_tests` | Pointer coordinates clamping, button masks, wheel delta, 256-key matrix, modifier flags, blur release | 0.02s | **PASSED** |
| `841` | `stage225_10_audio_service_tests` | PCM 16-bit stereo buffers, 256 KB ring buffer, submission limits (64 KB), drain simulation, underrun flags | 0.02s | **PASSED** |
| `842` | `stage225_12_sensor_service_tests` | Accelerometer, gyroscope, ambient light, synthetic waveforms, rate limiting [10ms, 1000ms], subscriptions | 0.03s | **PASSED** |
| `843` | `stage225_14_network_service_tests` | Ethernet frame handling, MTU (1514), queue capacity (64 packets), loopback echo, packet drops | 0.03s | **PASSED** |
| `844` | `stage225_16_clock_timer_tests` | Monotonic microsecond clock, UTC timespec, one-shot and periodic virtual timers, timer cancellations | 0.04s | **PASSED** |
| `845` | `stage225_18_notification_tests` | Ring buffer capacity (64), title/body truncation, duration auto-expiry, manual dismissal | 0.03s | **PASSED** |
| `846` | `stage225_21_device_center_tests` | Native ARM64 Device Center execution (PID 91, exit 91, 46,857 retired instructions) | 0.13s | **PASSED** |
| `847` | `stage225_21_system_monitor_tests` | Native ARM64 System Monitor execution (PID 92, exit 92, 13,296 retired instructions) | 0.13s | **PASSED** |
| `848` | `stage225_22_multi_app_device_tests` | Concurrent multi-process execution (PID 91 + PID 92 side-by-side, priority scheduler, isolated address spaces) | 0.15s | **PASSED** |
| `849` | `stage225_23_adversarial_tests` | Fuzzing, boundary violations, privilege escalation attempts, buffer flooding, rate manipulation | 0.06s | **PASSED** |

---

## 3. Full Regression Suite (Campaigns 223 – 225)
Execution command:
```powershell
ctest --test-dir D:\WinISO26\WinIOS\build -C RelWithDebInfo -R "stage223|stage224|stage225" --output-on-failure
```
**Output:**
```
100% tests passed out of 38
Total Test time (real) = 2.38 sec
```
Zero regressions detected across all 38 test suites.
