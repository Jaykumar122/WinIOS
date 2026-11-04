# WinIOS Phase 52 Stage 26 — External Event Injection & Blocker Protection

## 1. Supported Event Categories

External nondeterministic inputs must be injected through genuine subsystem architectural interfaces rather than direct CPU state mutations.

| Event Type | Numeric | Injection Mechanism | Subsystem Target |
|---|---|---|---|
| `ExternalInterruptAsserted` | 30 | `scheduler->interrupt_router().request_interrupt()` | `SmpInterruptRouter` |
| `InterruptRouted` | 31 | Verified against routing state | `SmpInterruptRouter` |
| `InterruptAccepted` | 32 | Verified on CPU exception boundary | Virtual Core |
| `TimerComparatorExpired` | 43 | Comparator match trigger | `VirtualTime` / GIC Timer |
| `ReservationSet` | 61 | Tracked exclusive reservation | `SmpExclusiveMonitorManager` |
| `ReservationCleared` | 62 | Cleared reservation | `SmpExclusiveMonitorManager` |
| `MemoryBarrierSync` | 60 | Memory synchronization barrier | `SmpSharedMemory` |

---

## 2. Active Replay Blockers: BLK-SMP-03 & BLK-SMP-04

Stage 25 identified two high-severity blockers preventing unrestricted multicore replay:

### 2.1 BLK-SMP-03: Asynchronous Host DMA
- **Problem:** Host hardware or network background threads that write directly to guest memory asynchronously without guest instruction involvement cannot be deterministically replayed unless every byte and timestamp is recorded.
- **Enforcement:**
  - `SmpReplayCapabilityConfig::allow_uncaptured_dma = false`.
  - If `HostInputInjected` is observed in an SNDL v2 recording during replay, the engine immediately halts with `SmpReplayDivergenceType::UnsupportedExternalInput`.
  - The replay capability gate rejects workloads requiring uncaptured host DMA.

### 2.2 BLK-SMP-04: Host Wall-Clock Reads
- **Problem:** Direct reads of host wall-clock APIs (`std::chrono`, `QueryPerformanceCounter`) bypass virtual architectural time (`VirtualTime`) and yield nondeterministic values on repeated executions.
- **Enforcement:**
  - `SmpReplayCapabilityConfig::allow_unvirtualized_wall_clock = false`.
  - All time reads during deterministic replay must resolve strictly against virtual cycles (`cntpct_el0`, `cntvct_el0`) mapped to instruction retirements.
  - Workloads depending on host wall-clock reads are rejected by the capability gate.
