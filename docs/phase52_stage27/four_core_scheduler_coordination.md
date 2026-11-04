# Four-Core Scheduler Coordination

## 1. Multi-Core Round-Robin Scheduling

The WinIOS multicore scheduling model relies on a single host thread executing virtual cores in strict round-robin rotation. In Stage 27, this model coordinates across four virtual cores (Core IDs 0, 1, 2, 3).

### Scheduling Invariants
1. **Authoritative Core Selection**: The replay engine uses the live `SmpScheduler` to select cores, validating each selection against recorded `CoreSelected` events in SNDL v2.
2. **Fixed Instruction Budget ($Q$)**: Each slice receives a configured quantum budget (e.g. 100 instructions).
3. **Deterministic Truncation**: Slices that truncate early due to `CoreHalted`, `CoreWaitingForInterrupt` (WFI), `CoreWaitingForEvent` (WFE), or synchronous exceptions record and replay `EarlyTruncation` events.

---

## 2. Four-Core Coordination Protocol

```cpp
// 1. Peek and validate CoreSelected
if (next_event.event_type == SndlV2EventType::CoreSelected) {
    consume_expected_type(SndlV2EventType::CoreSelected, sel_rec);
    assert(sel_rec.core_id < active_core_count_); // 4 cores

    // 2. Consume QuantumStart
    consume_expected_type(SndlV2EventType::QuantumStart, qstart_rec);
    assert(qstart_rec.core_id == sel_rec.core_id);

    // 3. Step Core
    execute_core_quantum(sel_rec.core_id, qstart_rec.payload1);

    // 4. Validate QuantumEnd and EarlyTruncation
    consume_expected_type(SndlV2EventType::QuantumEnd, qend_rec);
}
```

This strict multi-point validation prevents schedule drift across the four virtual cores.
