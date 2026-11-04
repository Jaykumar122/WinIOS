# Baseline Snapshot Association & Validation

## 1. Cryptographic Binding to Cluster Snapshot (.wsnap v2)
Every SNDL v2 recording begins from a known, immutable architectural cluster state. Rather than embedding the entire multi-gigabyte snapshot into the log, the SNDL v2 header cryptographically binds to the baseline `.wsnap v2` file:

```cpp
struct alignas(8) WSndlV2Header {
    std::array<std::uint8_t, 8> magic;       // 'WSND2\0\1\0'
    std::uint16_t version;                  // 2
    std::uint16_t core_count;               // Exact core count (e.g. 2, 4, 8)
    std::uint16_t flags;
    std::uint16_t compression_type;
    std::uint64_t base_snapshot_hash;       // Canonical hash from SmpScheduler
    std::uint64_t base_snapshot_file_size;  // Exact file size in bytes
    std::uint64_t memory_capacity;          // Guest physical memory size in bytes
    ...
};
```

---

## 2. Validation Invariants
When a user or test provides a baseline `.wsnap v2` path to `SndlV2Validator::validate_file(sndl_path, wsnap_path)`:
1. **File Existence**: Verifies snapshot file exists on disk.
2. **Snapshot Header Inspection**: Reads and validates the 64-byte `WSnapHeader`.
3. **Core Count Equality**: `snapshot.core_count == sndl.core_count`.
4. **Memory Capacity Equality**: `snapshot.memory_capacity == sndl.memory_capacity`.
5. **Canonical Hash Matching**: `snapshot.canonical_hash == sndl.base_snapshot_hash` (when nonzero).

Any discrepancy results in immediate rejection with `SndlV2ErrorCode::SnapshotMismatch`, preventing recording replay from an incompatible or corrupted initial state.
