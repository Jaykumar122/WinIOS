# First-Divergence Diagnostics & Root-Cause Payload Architecture

## 1. Overview
When deterministic replay encounters a discrepancy, immediate root-cause isolation is critical. The `SmpReplayDivergence` structure captures comprehensive diagnostic telemetry at the first point of divergence.

## 2. Divergence Diagnostic Payload
```cpp
struct SmpReplayDivergence {
    SmpReplayDivergenceType type{SmpReplayDivergenceType::None};
    std::string message;
    std::uint64_t global_event_sequence{0};
    std::uint64_t global_retirement_sequence{0};
    std::uint64_t core_retirement_count{0};
    std::uint32_t core_id{0};
    sndl::SndlV2EventType expected_event_type{sndl::SndlV2EventType::Unknown};
    sndl::SndlV2EventType actual_event_type{sndl::SndlV2EventType::Unknown};
    std::uint64_t expected_val{0};
    std::uint64_t actual_val{0};
    std::uint64_t quantum_budget{0};
    std::uint64_t quantum_executed{0};
    std::uint64_t pc{0};
};
```

## 3. Diagnostic Capabilities
1. **Pinpointing Event Discrepancies**: Flags the exact event sequence number where expected vs actual event types diverged.
2. **Instruction Parity**: Pinpoints exact core ID and instruction counts where retirement counts diverged.
3. **Core Attribution**: Identifies which virtual core triggered the divergence.
4. **Terminal State**: Replay coordinator transitions to `SmpReplayState::Diverged`, halting further state corruption.
