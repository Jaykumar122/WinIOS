# Host Wall-Clock Virtualization & BLK-SMP-04 Resolution

## 1. Overview of BLK-SMP-04

Blocker **BLK-SMP-04** pertains to non-deterministic host wall-clock reads bypassing simulated virtual counter (`CNTVCT_EL0`) and exposing host thread timing or latency jitter to guest execution.

In Stage 27, an exhaustive audit of all time sources across WinIOS was conducted to categorize every clock source into:
1. **Deterministic Virtual Clock**: Virtual-time-driven counters tied to instruction retirement.
2. **Recorded External Clock**: Asynchronous time inputs captured in SNDL streams.
3. **Unsupported Host Clock**: Live host queries (`std::chrono`, `GetTickCount`, `QueryPerformanceCounter`).

---

## 2. Exhaustive Audit of Guest Time Sources

| Time Source / Register | Guest-Visible? | Implementation | Audit Finding |
|---|---|---|---|
| `CNTPCT_EL0` | Yes | `VirtualTime::get_physical_counter()` | 100% deterministic; bound to instruction retirement |
| `CNTVCT_EL0` | Yes | `VirtualTime::get_virtual_counter(core_id)` | 100% deterministic; `phys - cntvoff_el2` |
| `CNTFRQ_EL0` | Yes | `VirtualTime::get_frequency()` | Architectural constant (default 24 MHz) |
| `CNTP_CTL_EL0` | Yes | `VirtualTime` core timer state | Deterministic comparator evaluation |
| `CNTP_TVAL_EL0` | Yes | `VirtualTime::read_phys_tval()` | Monotonic offset from virtual counter |
| `CNTV_CTL_EL0` | Yes | `VirtualTime` core timer state | Deterministic comparator evaluation |
| `CNTV_TVAL_EL0` | Yes | `VirtualTime::read_virt_tval()` | Monotonic offset from virtual counter |
| Host `std::chrono` | **No** | Profilers & headers only | Never exposed to guest registers |
| Host `GetTickCount64` | **No** | Process launcher only | Never exposed to guest registers |

---

## 3. Clock Capability Policy Enforcement

```cpp
enum class SmpReplayClockSource : std::uint32_t {
    DeterministicVirtualClock = 0,
    RecordedExternalClock     = 1,
    UnsupportedHostClock      = 2
};
```

1. **Deterministic Default**: The engine operates under `DeterministicVirtualClock`.
2. **Rejection of Unvirtualized Wall-Clock**: If `allow_unvirtualized_wall_clock == true`, preparation fails immediately with `ReplayErrorCode::UnsupportedConfiguration`.
3. **Rejection of Unsupported Clock**: If `clock_source == UnsupportedHostClock`, preparation fails immediately with `ReplayErrorCode::UnsupportedConfiguration`.
4. **Resolution Status**: **BLK-SMP-04 IS RESOLVED** for all supported guest execution paths.
