# WinIOS Phase 52 Stage 26 — Replay Divergence Detection Specification

## 1. Divergence Classification

Divergence detection in Stage 26 is structured, fail-fast, and diagnostic. If an architectural deviation occurs during execution, the engine populates `SmpReplayDivergence` and immediately transitions to `SmpReplayState::Diverged`.

```cpp
struct SmpReplayDivergence {
    SmpReplayDivergenceType type;
    std::string message;
    std::uint64_t global_event_sequence;
    std::uint64_t global_retirement_sequence;
    std::uint64_t core_retirement_count;
    std::uint32_t core_id;
    SndlV2EventType expected_event_type;
    SndlV2EventType actual_event_type;
    std::uint64_t expected_val;
    std::uint64_t actual_val;
    std::uint64_t quantum_budget;
    std::uint64_t quantum_executed;
    std::uint64_t pc;
};
```

---

## 2. Divergence Taxonomy & Triggers

| Category | Trigger Condition | Diagnostic Actions |
|---|---|---|
| `UnexpectedEvent` | Next event in cursor does not match expected execution phase. | Records expected vs actual event type. |
| `MissingEvent` | Replay reached quantum/instruction boundary, but event stream is exhausted. | Records current global sequence and boundary type. |
| `SequenceMismatch` | Event sequence non-monotonic or non-consecutive ($S_{i+1} \neq S_i + 1$). | Records expected sequence vs observed sequence. |
| `CoreMismatch` | Event attributed to core $c_1$, but executed on core $c_2$. | Records target core ID vs actual core ID. |
| `SchedulerMismatch` | Round-robin scheduler selected different core than recorded `CoreSelected`. | Records actual scheduler index vs recorded core ID. |
| `QuantumMismatch` | Quantum truncation reason differs from recorded reason. | Records expected reason code vs actual reason code. |
| `RetirementMismatch` | Slice or final instruction retirement count differs from recording. | Records expected retirement count vs actual retirement count. |
| `InterruptMismatch` | Interrupt delivery rejected or delivered to wrong core. | Records interrupt ID and target core. |
| `SnapshotMismatch` | Restored cluster hash does not match baseline snapshot hash. | Records expected cluster hash vs restored hash. |
| `FinalStateMismatch` | Final cluster hash does not match expected reference hash. | Records expected final hash vs actual final hash. |
| `UnsupportedExternalInput` | Workload contains uncaptured DMA (`BLK-SMP-03`) or unvirtualized wall clock (`BLK-SMP-04`). | Halts immediately and flags capability violation. |
| `IncompleteRecording` | Missing footer, uncompleted status, or dropped events in recording. | Records footer completion status and dropped count. |
