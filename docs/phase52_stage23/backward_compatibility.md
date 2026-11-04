# Backward Compatibility & Format Isolation

## 1. Subsystem Compatibility Guarantees

Phase 52 Stage 23 introduces version 2 of the `.wsnap` snapshot container while maintaining 100% binary and API compatibility with all pre-existing formats and subsystems:

| Format / Subsystem | Current Version | Stage 23 Status | Backward Compatibility Guarantee |
|---|---|---|---|
| Single-Core Snapshot | `.wsnap` v1 | Active & Unchanged | Existing `.wsnap` v1 files continue to load and restore seamlessly. `SnapshotManager::capture_to_file` and `restore_from_file` remain functional. |
| Non-Deterministic Log | `.sndl` v1 | Active & Unchanged | NDL format schema remains version 1. No schema modifications made. |
| Diagnostic Trace | `.wtr` v1 | Active & Unchanged | 64-byte `TraceEvent` schema and `.wtr` binary serialization remain 100% intact. |
| Single-Core Replay | `ReplayEngine` | Active & Protected | Single-core replay execution, checkpointing, and divergence detection operate identically. |
| Multi-Core Cluster Snapshot | `.wsnap` v2 | Newly Introduced | Cluster snapshots use `version = 2` with new section types (`0x11`–`0x17`). |

---

## 2. Format Distinguishability & Isolation Rules

1. **Old Software Protection**: Legacy single-core software calling `decode_wsnap_header()` requires `version == 1`. When encountering a `.wsnap` v2 cluster snapshot, it cleanly rejects it with `SnapshotErrorCode::UnsupportedVersion`. Old software cannot silently misinterpret cluster snapshots.
2. **Cluster Tooling**: Cluster snapshot routines (`SmpSnapshotManager`) require `version == 2` via `decode_smp_wsnap_header()`.
3. **SMP Replay Rejection**: If a user attempts to pass a `.wsnap` v2 file to the replay engine, `ReplaySession::prepare` checks the core count in the header and immediately terminates with `ReplayErrorCode::UnsupportedConfiguration`.

---

## 3. Regression Test Verification

All 58 tests in the CTest test suite pass in both Debug and Release configurations, confirming:
- Single-core `.wsnap` v1 creation, inspection, validation, and restoration pass (Test Group J, `snapshot_tests`).
- `.sndl` v1 recording and validation pass (`sndl_tests`).
- Trace recording, serialization, and analysis pass (`trace_*_tests`).
- Replay tests and hardening benchmarks pass (`replay_tests`, `replay_hardening_tests`, `replay_scalability_tests`).
