# Eight-Core Event Consumption & Stream Synchronization

## 1. Multi-Stream SNDL v2 Event Processing
During 8-core deterministic replay, the event stream combines interleaved event streams across 8 virtual cores into a single canonical global event sequence $S = \langle e_1, e_2, \dots, e_N \rangle$.

## 2. Event Dispatching
Each event read by `SmpReplayEventCursor` is validated and dispatched according to its `SndlV2EventType`:
1. **Core Lifecycle**: `CoreCreated`, `CoreStarted`, `CoreHalted`, `CoreEnteredWfi`, `CoreEnteredWfe`, `CoreResumed`, `CoreStopped`.
2. **Scheduler Boundaries**: `CoreSelected`, `QuantumStart`, `QuantumEnd`, `EarlyTruncation`, `BarrierEntry`, `BarrierRelease`.
3. **Interrupts & Exceptions**: `ExternalInterruptAsserted`, `InterruptRouted`, `InterruptAccepted`, `ExceptionTaken`, `ExceptionReturned`.
4. **Architectural Timers**: `CntpctRead`, `CntvctRead`, `PmccntrRead`, `TimerComparatorExpired`.
5. **MMIO & Entropy**: `MmioReadResult`, `EntropyReadResult`, `HostInputInjected`.
6. **Shared Memory & Monitors**: `MemoryBarrierSync`, `ReservationSet`, `ReservationCleared`, `StoreExclusiveResult`.

## 3. Strict Sequence Monotonicity
The cursor enforces $S_{i+1} = S_i + 1$ across all blocks. Any gap or non-monotonicity triggers an immediate divergence:
```cpp
if (config_.enforce_strict_monotonicity && rec.global_event_sequence != expected_sequence) {
    trigger_divergence(SmpReplayDivergenceType::SequenceMismatch);
}
```
Out-of-bounds core IDs ($core\_id \ge 8$) are caught and rejected prior to execution.
