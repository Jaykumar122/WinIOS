# WinIOS — Phase 52, Stage 24: Single-Core Compatibility & Replay Invariants

## 1. Compatibility Policy
The introduction of `.sndl v2` strictly preserves 100% binary, structural, and behavioral compatibility with earlier recording and snapshot versions:
- `.sndl v1` single-core recording files continue to be processed by `SndlRecorder`, `SndlValidator`, and the single-core `ReplayEngine`.
- `.wsnap v1` single-core architectural snapshots remain valid and restorable.
- `.wsnap v2` multi-core snapshots remain valid and restorable.
- `.wtr v1` diagnostic traces remain unchanged.

## 2. Replay Engine Scope and Rejection
Stage 24 is **explicitly bounded to multicore recording and validation**.
Deterministic multicore replay is NOT implemented or permitted in Stage 24.

The single-core replay engine retains its strict security invariant:
```cpp
if (snapshot.header.core_count > 1 || sndl_header.core_count > 1) {
    return ReplayResult::Error(ReplayErrorCode::UnsupportedConfiguration);
}
```
Any attempt to supply a multi-core `.sndl v2` or `.wsnap v2` recording to the Stage 20/21 single-core replay engine is rejected deterministically with `UnsupportedConfiguration`.
