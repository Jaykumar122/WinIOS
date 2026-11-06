# Eight-Core Replay Rejection
## Verification of Strict Eight-Core Replay Capability Gating at All Entry Points

---

## 1. Replay Capability Policy

Stage 28 explicitly generalizes execution, scheduling, shared memory, and snapshotting for 8 cores, but **strictly prohibits eight-core deterministic replay**.

The replay capability gate mandates that any attempt to prepare or execute replay on a recording or snapshot configured with more than 4 cores must fail immediately and deterministically.

---

## 2. Rejection Entry Points Verified

1. **`SmpReplayCoordinator::prepare()`**:
   - Inspects `topology.core_count` from `.wsnap v2` header.
   - If `core_count > 4`, immediately halts with `ReplayErrorCode::UnsupportedConfiguration`.
   - Verified in `smp_topology_scalability_tests` Test Group 10.

2. **`ReplaySession::prepare()`**:
   - Replay session entry point checks core count against supported replay topologies.
   - Core count of 8 returns `ReplayErrorCode::UnsupportedConfiguration`.

3. **`win_ios.exe replay run` CLI**:
   - Validates `.wsnap v2` and `.sndl v2` headers prior to replay execution.
   - Rejects 8-core configurations with descriptive diagnostic error message.

---

## 3. Rejection Invariant

In Stage 28, the rejection condition:
```cpp
if (topology.core_count > 4) {
    return ReplayErrorCode::UnsupportedConfiguration;
}
```
is enforced unconditionally across all replay preparation pathways.
