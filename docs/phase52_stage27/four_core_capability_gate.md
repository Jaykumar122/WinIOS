# Four-Core Replay Capability Gate

## 1. Capability Gate Policy Specification

Stage 27 expands the multi-core deterministic replay capability gate to safely admit 4-core workloads while maintaining conservative invariants.

### Policy Table

| Core Count | Capability Policy | Error Code / Result |
|:---:|---|---|
| **1** | Existing single-core deterministic replay | Allowed (`ReplaySession`) |
| **2** | Deterministic 2-core replay (`allow_experimental_smp = true`) | Allowed (`SmpReplayCoordinator`) |
| **4** | Experimental 4-core replay (`allow_experimental_smp = true && allow_four_core_replay = true`) | Allowed (`SmpReplayCoordinator`) |
| **8** | Unconditionally Rejected | `ReplayErrorCode::UnsupportedConfiguration` |
| **Other** | Any other core count ($N \notin \{1, 2, 4\}$) | `ReplayErrorCode::UnsupportedConfiguration` |

---

## 2. Capability Configuration Structure

```cpp
struct SmpReplayCapabilityConfig {
    std::size_t max_supported_cores{2};
    bool allow_experimental_smp{false};
    bool allow_four_core_replay{false};
    SmpReplayClockSource clock_source{SmpReplayClockSource::DeterministicVirtualClock};
    bool allow_unvirtualized_wall_clock{false};
    bool allow_uncaptured_dma{false};
    bool require_completed_footer{true};
    bool require_snapshot_association{true};
    bool enforce_strict_monotonicity{true};
    bool enforce_quantum_matching{true};
    bool enforce_retirement_parity{true};
    bool verify_final_cluster_hash{true};
};
```

---

## 3. Enforcement Points

1. **Snapshot Pre-Flight Check**:
   - Inspects `snap_info.header.core_count`.
   - If 8: immediately rejects with `UnsupportedConfiguration`.
   - If 4: requires `allow_four_core_replay == true`.
   - If not 2 or 4: rejects with `UnsupportedConfiguration`.

2. **SNDL Recording Check**:
   - Inspects `cursor_.header().core_count`.
   - If 8: rejects with `UnsupportedConfiguration`.
   - If core count does not match snapshot: rejects with `UnsupportedConfiguration`.

3. **Blocker Guards**:
   - `allow_unvirtualized_wall_clock == true`: rejected (`BLK-SMP-04`).
   - `clock_source == UnsupportedHostClock`: rejected (`BLK-SMP-04`).
   - `allow_uncaptured_dma == true`: rejected (`BLK-SMP-03`).
