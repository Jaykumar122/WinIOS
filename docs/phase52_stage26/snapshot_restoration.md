# WinIOS Phase 52 Stage 26 — Baseline Snapshot Restoration Specification

## 1. Multi-Core Cluster Restoration Protocol

Before deterministic replay can begin, the target cluster must be initialized to the exact state represented by the associated `.wsnap v2` baseline snapshot.

```
                    +------------------------------------+
                    |        .wsnap v2 Snapshot          |
                    +------------------------------------+
                                      |
                         SmpSnapshotManager::inspect()
                                      |
                         1. Check core_count == 2
                         2. Check base_snapshot_hash matches
                         3. Validate directory & CRCs
                                      |
                         SmpSnapshotManager::restore()
                                      |
                         1. Restore shared memory chunks
                         2. Restore per-core CPU registers
                         3. Restore exclusive monitor states
                         4. Restore interrupt router queues
                         5. Restore scheduler state & sequence
                                      |
                                      v
                    +------------------------------------+
                    |       Re-compute Canonical         |
                    |           Cluster Hash             |
                    +------------------------------------+
                                      |
                          Hash == snap.hash?
                         /                  \
                       YES                   NO
                        v                     v
                    [Ready]             [Diverged]
```

---

## 2. Verification Steps

1. **Snapshot Pre-Flight Validation:** `SmpSnapshotManager::validate_file()` performs complete structural and CRC32C verification of header, section directory, and payload chunks.
2. **Topology Verification:** Verifies `header.core_count == 2` and `header.memory_capacity` matches configuration.
3. **Association Verification:** Verifies `cursor.header().base_snapshot_hash == snap_info.integrity_data.canonical_cluster_hash`.
4. **State Commit:** Restores shared memory, per-core CPU registers/system registers, exclusive monitors, interrupt router, and scheduler configuration into the live cluster.
5. **Re-Hash Parity:** The live cluster immediately calculates `scheduler->compute_canonical_multicore_hash()`. If the restored cluster hash differs by even one bit from the recorded snapshot hash, preparation aborts with `ReplayErrorCode::SnapshotMismatch`.
