# Phase 52, Stage 27 — Architecture Specification
## Four-Core Deterministic SMP Replay, External Input Capture, DMA Virtualization & Scalability Validation

---

## 1. System Overview

Phase 52, Stage 27 establishes **Four-Core Deterministic Replay** in WinIOS while hardening external input capture, virtualizing host time sources (resolving **BLK-SMP-04**), enforcing strict DMA capability rejection guards (mitigating **BLK-SMP-03**), and strictly rejecting eight-core replay.

```
       +-------------------------------------------------------------+
       |                  SmpReplayCoordinator                       |
       |  - Replay State Machine (Uninit->Prepared->Running->Done)   |
       |  - 4-Core Controlled Capability Gate                        |
       |  - Clock Source Policy: DeterministicVirtualClock           |
       |  - BLK-SMP-03 & BLK-SMP-04 Rejection Guards                |
       +------------------------------+------------------------------+
                                      |
         +----------------------------+----------------------------+
         |                                                         |
         v                                                         v
+------------------+                                      +------------------+
|  .wsnap v2 File  |                                      |  .sndl v2 File   |
| (4-Core State)   |                                      | (4-Core Events)  |
+--------+---------+                                      +--------+---------+
         |                                                         |
         v                                                         v
+------------------------+                                +------------------------+
|  SmpSnapshotRestorer   |                                |   SndlV2ReplayReader   |
|  - 4-Core RAM restore  |                                |   - Bounded streaming  |
|  - Per-core registers  |                                |   - CRC32C verify      |
|  - Canonical hash H0   |                                +-----------+------------+
+------------------------+                                            |
                                                                      v
                                                          +------------------------+
                                                          |  SmpReplayEventCursor  |
                                                          |  - Strict monotonicity|
                                                          |  - Lookahead peek      |
                                                          +-----------+------------+
                                                                      |
+---------------------------------------------------------------------+
|
v
+--------------------------------------------------------------------------+
| 4-Core Scheduler & Execution Replay Loop (Single Host Thread)            |
|                                                                          |
| 1. Authoritative SmpScheduler selects next core among cores 0..3.       |
| 2. Replay cursor validates CoreSelected and QuantumStart matches.        |
| 3. Step active core instructions, advancing R_core[i] and R_global.     |
| 4. External event delivery (MMIO, Interrupts, Timers, Exclusives).      |
| 5. Validate QuantumEnd and EarlyTruncation bounds.                      |
| 6. Compute final canonical multicore hash at completion.                 |
+--------------------------------------------------------------------------+
```

---

## 2. Core Subsystems

### 2.1 Capability Gate & Clock Source Policies
- Gated core counts: 1 (single-core engine), 2 (Stage 26 verified), 4 (Stage 27 experimental opt-in via `allow_four_core_replay = true`).
- 8-core and arbitrary topologies remain strictly rejected with `ReplayErrorCode::UnsupportedConfiguration`.
- Time policy: All guest architectural time derives from `VirtualTime` (`DeterministicVirtualClock`). Host wall-clock reads are forbidden (`BLK-SMP-04`).
- DMA policy: Since WinIOS currently possesses no DMA peripherals, uncaptured DMA is rejected (`BLK-SMP-03`).

### 2.2 Shared Memory & 64-Byte Exclusive Granule
- Single-host-thread deterministic interleaving across cores 0, 1, 2, 3.
- `SmpExclusiveMonitorManager` tracks exclusive reservations per core on 64-byte aligned granules.
- Cross-core store operations invalidate conflicting reservations deterministically.

### 2.3 Retirement Accounting
- Global monotonic retirement clock ($R_{global}$).
- Per-core monotonic retirement counters ($R_{core}[0..3]$).
- Exception taken in step guarantees retirement counter does not increment.
