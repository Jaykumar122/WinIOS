# Eight-Core Snapshot Restoration & Canonical State Binding

## 1. Architectural Snapshot v2 Restoration
The `.wsnap v2` snapshot format encapsulates:
- 64-byte `WSnapHeader` with `core_count = 8`.
- Per-core register files, system registers, and lifecycle states for 8 cores.
- Physical memory pages with run-length encoding.
- Global retirement sequence and per-core retirement counters.
- 64-byte `WSnapIntegrityData` containing SHA-256 and CRC32C hashes of all blocks and a 64-bit `canonical_cluster_hash`.

## 2. Cluster State Restoration Flow
1. `validate_file()` verifies snapshot magic, version, block boundaries, and integrity hashes.
2. `restore_from_file()` instantiates 8 virtual cores and shared memory in `SmpScheduler`.
3. Memory and core states are mapped bit-for-bit into the guest memory space.
4. `compute_canonical_multicore_hash()` computes the active initial state hash.
5. If `require_snapshot_association = true`, the hash is checked against the SNDL v2 header `base_snapshot_hash`.
6. Any discrepancy causes preparation to fail with `ReplayErrorCode::SnapshotMismatch`.
