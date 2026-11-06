# Eight-Core Architectural Timer Replay & Virtual Clock Protection

## 1. VirtualTime Clock Source
In WinIOS SMP replay, time is virtualized strictly via `VirtualTime` derived from instruction retirement counts. Physical host wall clocks are prohibited (BLK-SMP-04 protection).

## 2. Timer Event Capture
Guest reads of architectural timer registers (`CNTPCT_EL0`, `CNTVCT_EL0`, `PMCCNTR_EL0`) and timer expiry events (`TimerComparatorExpired`) are captured as explicit SNDL v2 records:
- `CntpctRead`: Core ID, retirement sequence, 64-bit counter value.
- `CntvctRead`: Core ID, retirement sequence, 64-bit virtual counter value.
- `TimerComparatorExpired`: Comparator register threshold and triggering core.

## 3. Host Clock Rejection Gate
If replay configuration attempts to use `SmpReplayClockSource::UnsupportedHostClock` or sets `allow_unvirtualized_wall_clock = true`, preparation immediately aborts with `ReplayErrorCode::UnsupportedConfiguration`, guaranteeing that non-deterministic host timing cannot contaminate the replay.
