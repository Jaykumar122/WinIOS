# Multicore Replay Failure Injection & Recovery Diagnostics

## 1. Divergence and Inconsistency Handling
The multicore replay engine enforces strict invariants during recording stream ingestion. When an invariant fails, execution halts immediately with structured diagnostic telemetry:

1. **First-Divergence Detection**: If an event record's global event sequence or retirement count does not match the active scheduler state, replay terminates prior to applying corrupt state.
2. **Snapshot Inconsistency**: If the base snapshot file size or SHA-256 / MurmurHash3 does not match the SNDL v2 header metadata, `prepare()` returns `SmpReplayStatus::SnapshotMismatch` without executing guest code.
3. **Truncated Event Streams**: If the recording terminates prematurely (e.g. power failure during recording), `SndlV2ReplayReader` detects unexpected EOF or incomplete block and reports `StreamTruncated`.
4. **State Immutability Guarantee**: In every failure injection test, any core that was not active prior to failure remained untouched, and no partial state corrupted host structures.
