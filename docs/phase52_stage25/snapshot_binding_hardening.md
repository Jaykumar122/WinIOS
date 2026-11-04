# WinIOS — Phase 52, Stage 25: Snapshot Binding & Canonical Hash Hardening

## 1. Multi-Core Cluster Snapshot Identity
To ensure that an `.sndl v2` execution recording can only ever be replayed from the exact cluster state from which it was recorded, Stage 25 strengthens the binding between `.sndl v2` and `.wsnap v2`:

1. **Snapshot Identity Fields in `WSndlV2Header`**:
   - `core_count`: Cluster core count ($1 \le N \le 256$).
   - `memory_capacity`: Guest physical RAM size.
   - `base_snapshot_hash`: 64-bit FNV-1a canonical cluster hash computed across all core register files, system registers, and shared physical memory.
   - `base_snapshot_file_size`: Byte size of the source `.wsnap v2` file.

## 2. Integrated Snapshot Inspection
`SndlV2Validator::validate_file` invokes `winios::snapshot::SmpSnapshotManager::inspect_file` to thoroughly verify:
- Snapshot file presence: Missing file returns `SndlV2ErrorCode::SnapshotNotFound`.
- Snapshot structural integrity: Corrupt snapshot returns `SndlV2ErrorCode::SnapshotMismatch` ("Snapshot malformed").
- Format version: Must be $\ge 2$.
- Core count matching: Snapshot core count must exactly match header `core_count`.
- Memory capacity matching: Snapshot RAM size must match header `memory_capacity`.
- **Canonical Cluster Hash Matching**: `snap_info.integrity_data.canonical_cluster_hash` must match `hdr.base_snapshot_hash`.
