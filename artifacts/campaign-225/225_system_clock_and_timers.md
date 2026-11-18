# WinIOS Campaign 225 — Stage 225.16 Virtual System Clock, High-Resolution Timers & Scheduling

## 1. Clock Architecture Overview
The `VirtualClockService` ([src/devices/clock_service.h](file:///D:/WinISO26/WinIOS/src/devices/clock_service.h), [src/devices/clock_service.cpp](file:///D:/WinISO26/WinIOS/src/devices/clock_service.cpp)) provides dual timing domains for guest applications:
1. **Monotonic Steady Clock:** Microsecond-resolution counter tracking continuous guest execution time since boot, immune to host wall-clock adjustments or leap seconds.
2. **Real-Time Wall Clock:** UTC timestamp synchronized with the platform's epoch base, formatted as standard `timespec` (seconds and nanoseconds).

---

## 2. Timer Subsystem & High-Resolution Scheduling
In addition to passive clock queries, the service manages a bounded, thread-safe virtual timer pool:
- Maximum Active Timers: $128$ concurrent timers across all guest processes.
- Modes:
  - **One-Shot Timers:** Trigger once when expiration threshold is reached, then transition to expired state.
  - **Periodic Timers:** Automatically advance next trigger threshold by `interval_us` upon firing.
- Precision: Evaluated at microsecond granularity during CPU step loops and scheduler ticks.

---

## 3. Syscall API Dispatch
- Syscall 552 (`SYS_WINIOS_TIME_GET`): Queries monotonic and UTC timestamps.
  ```c
  int32_t winios_time_get(winios_timespec_t* out_ts);
  ```
- Syscall 553 (`SYS_WINIOS_TIMER_SET`): Schedules or updates a high-resolution virtual timer.
  ```c
  uint32_t winios_timer_set(uint32_t timer_id, uint64_t interval_us, uint32_t periodic);
  ```

---

## 4. Verification Evidence
- [tests/unit/stage225_16_clock_timer_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_16_clock_timer_tests.cpp): Validates monotonic advance, UTC accuracy, timer scheduling, one-shot vs periodic firing, timer cancellation, and resource bounds enforcement.
