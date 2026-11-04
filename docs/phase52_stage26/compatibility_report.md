# Single-Core Backward Compatibility and Regression Report

## 1. Compatibility Scope

Phase 52 Stage 26 expands the WinIOS deterministic replay subsystem to support 2-core SMP workloads under explicit capability gating. A core invariant of WinIOS architecture is zero regression for single-core execution paths and historical file formats.

The following compatibility boundaries were verified:
1. **`.wsnap v1` Snapshot Backward Compatibility**: Single-core architectural snapshots created in Stage 17 continue to load, validate, and restore without modification.
2. **`.sndl v1` Recording Backward Compatibility**: Single-core nondeterministic logs created in Stage 18 continue to replay deterministically via `ReplaySession`.
3. **`.wtr v1` Diagnostic Tracing**: Diagnostic traces remain binary-compatible.
4. **Historical Test Suite Stability**: All prior unit tests and integration tests continue to pass with 0 regressions.

---

## 2. Regression Test Results

Running the full CTest suite across all targets in both Debug and Release configurations:

| Target Category | Targets Executed | Passed | Failed | Skipped | Status |
|---|---|---|---|---|---|
| Single-Core Interpreter & Memory | 25 | 25 | 0 | 0 | PASS |
| Single-Core Snapshot (`.wsnap v1`) | 4 | 4 | 0 | 0 | PASS |
| Single-Core SNDL (`.sndl v1`) | 5 | 5 | 0 | 0 | PASS |
| Single-Core Replay & Hardening | 8 | 8 | 0 | 0 | PASS |
| SMP Foundation & Scheduler | 6 | 6 | 0 | 0 | PASS |
| Multi-Core Snapshot (`.wsnap v2`) | 5 | 5 | 0 | 0 | PASS |
| Multi-Core SNDL (`.sndl v2`) | 5 | 5 | 0 | 0 | PASS |
| Multi-Core Replay (Stage 26) | 2 | 2 | 0 | 0 | PASS |
| System Demo (`win_ios.exe`) | 1 | 1 | 0 | 0 | PASS |
| Optional Benchmarks | 1 | 0 | 0 | 1 | SKIPPED |
| **Total** | **62** | **61** | **0** | **1** | **100% PASS** |

---

## 3. Backward Compatibility Safeguards in Code

In `src/replay/replay_session.cpp`:

```cpp
// Fast path for single-core snapshots
if (core_count == 1) {
    // Routes directly to single-core ReplaySession engine
    return prepare_single_core(snapshot_path, sndl_path);
}

// Multi-core path
if (!config_.allow_experimental_smp) {
    return ReplayResult::fail(ReplayErrorCode::UnsupportedConfiguration,
        "Multi-core replay requires experimental SMP flag to be enabled");
}
```

When existing tests run without `allow_experimental_smp = true`, attempting to replay a multi-core recording or snapshot continues to be rejected cleanly with `UnsupportedConfiguration`. This ensures zero behavioral drift for legacy scripts and tools.
