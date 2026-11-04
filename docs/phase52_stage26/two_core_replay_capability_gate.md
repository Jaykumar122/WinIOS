# Two-Core Replay Capability Gate

## 1. Executive Summary

Phase 52 Stage 26 introduces a strict **Capability Gate** controlling multi-core deterministic replay. While multi-core deterministic replay has been validated on 2-core workloads with round-robin quantum scheduling, arbitrary multi-core configurations (specifically 4-core and 8-core topologies) remain disabled and rejected.

The capability gate serves as an architectural enforcement boundary preventing unverified or non-deterministic executions from running under the replay engine.

---

## 2. Capability Gate Policies and Configuration

The capability gate is evaluated during the replay preparation phase (`ReplaySession::prepare` and `SmpReplayCoordinator::prepare`).

```cpp
struct SmpReplayCapabilityConfig {
    uint32_t supported_core_count = 2;
    bool allow_experimental_smp = false;
    bool permit_host_dma = false;         // BLK-SMP-03: must remain false
    bool permit_host_wall_clock = false;  // BLK-SMP-04: must remain false
};
```

### Policy Rules

1. **Core Count Validation**:
   - Single-core ($N=1$): Handled by the legacy single-core replay engine (`replay_session.cpp`).
   - Two-core ($N=2$): Permitted **if and only if** `allow_experimental_smp == true`.
   - Four-core ($N=4$) and Eight-core ($N=8$): **Strictly Rejected** with error code `ReplayErrorCode::UnsupportedConfiguration`.
   - Any $N > 2$: **Strictly Rejected**.

2. **Experimental SMP Flag**:
   - If `core_count > 1` and `allow_experimental_smp == false`, the engine immediately rejects the session with `ReplayErrorCode::UnsupportedConfiguration`.

3. **Blocker Invariants**:
   - If `permit_host_dma == true`, preparation fails immediately (`UnsupportedConfiguration`).
   - If `permit_host_wall_clock == true`, preparation fails immediately (`UnsupportedConfiguration`).

---

## 3. Enforcement Implementation

In `src/replay/replay_session.cpp`:

```cpp
if (core_count > 1) {
    if (!config_.allow_experimental_smp) {
        return ReplayResult::fail(ReplayErrorCode::UnsupportedConfiguration,
            "Multi-core replay requires experimental SMP flag to be enabled");
    }
    if (core_count != 2) {
        return ReplayResult::fail(ReplayErrorCode::UnsupportedConfiguration,
            "SMP replay capability gate strictly limits execution to 2-core workloads (requested " + 
            std::to_string(core_count) + " cores)");
    }
    // Route to SmpReplayCoordinator
    ...
}
```

In `src/replay/smp_replay_coordinator.cpp`:

```cpp
if (core_count_ != 2) {
    return fail_prepare(ReplayErrorCode::UnsupportedConfiguration,
        "Capability gate violation: Only 2-core replay is currently supported, requested " +
        std::to_string(core_count_));
}
if (!cap_config_.allow_experimental_smp) {
    return fail_prepare(ReplayErrorCode::UnsupportedConfiguration,
        "Capability gate violation: experimental SMP flag is disabled");
}
if (cap_config_.permit_host_dma) {
    return fail_prepare(ReplayErrorCode::UnsupportedConfiguration,
        "BLK-SMP-03 violation: host DMA replay is unsupported");
}
if (cap_config_.permit_host_wall_clock) {
    return fail_prepare(ReplayErrorCode::UnsupportedConfiguration,
        "BLK-SMP-04 violation: host wall-clock replay is unsupported");
}
```

---

## 4. Test Verification

The capability gate is thoroughly verified in test group `Group A` and `Group I` of `smp_replay_tests.cpp`:

- **Group A (Gate Enforcement)**:
  - 2-core without `allow_experimental_smp` $\rightarrow$ Rejected (`UnsupportedConfiguration`).
  - 4-core with `allow_experimental_smp` $\rightarrow$ Rejected (`UnsupportedConfiguration`).
  - 8-core with `allow_experimental_smp` $\rightarrow$ Rejected (`UnsupportedConfiguration`).
  - 2-core with `permit_host_dma` $\rightarrow$ Rejected (`UnsupportedConfiguration`).
  - 2-core with `permit_host_wall_clock` $\rightarrow$ Rejected (`UnsupportedConfiguration`).
  - 2-core with `allow_experimental_smp == true` $\rightarrow$ Accepted (`Prepared`).

- **Historical Regression**:
  - `replay_tests.cpp` and `replay_hardening_tests.cpp` pass without regression. Default configurations reject SMP snapshots and sessions.
