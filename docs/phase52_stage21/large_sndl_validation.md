# Large SNDL Recording Validation & Integrity Analysis

## 1. Architectural Log Schema (`.sndl`)

The WinIOS Non-Deterministic Log (`.sndl`) records all external inputs required to reproduce single-core execution:
- **Container Header (64 bytes)**: Magic (`WSNDL\0\0\0`), version, flags, `base_snapshot_hash`, `core_count`, total instruction count, and header CRC32C.
- **Data Blocks (64 KB chunks)**: Each block begins with a 48-byte `WSndlBlockHeader` (block index, payload byte size, record count, flags, and block CRC32C) followed by contiguous 64-byte `SndlRecord` entries.

---

## 2. Large Recording Scale Matrix

The test and benchmark suites validated `.sndl` logs across five workload tiers:

| Workload Tier | Record Count | Total File Size | Blocks (64 KB) | Stream Validation Latency | Throughput | Integrity Status |
|---|---:|---:|---:|---:|---:|:---:|
| **Small** | 1,000 | 65.4 KB | 2 | 0.39 ms | 167 MB/s | **PASS** |
| **Medium** | 10,000 | 640.6 KB | 10 | 1.31 ms | 488 MB/s | **PASS** |
| **Large** | 100,000 | 6.25 MB | 98 | 11.07 ms | 565 MB/s | **PASS** |
| **Very Large** | 500,000 | 31.28 MB | 489 | 54.13 ms | 578 MB/s | **PASS** |
| **Stress** | 1,000,000 | 62.55 MB | 977 | 103.55 ms | **590 MB/s** | **PASS** |

---

## 3. Structural Integrity & Rejection Probes (Test Group E)

Adversarial tests on copies of `.sndl` recordings verified strict rejection of damaged logs:

```
                    Valid SNDL Stream (62.55 MB)
                                  │
       ┌──────────────────────────┼──────────────────────────┐
       ▼                          ▼                          ▼
[Bad Container Magic]     [Bad Container CRC]        [Snapshot Mismatch]
  Bytes 0..3 = "XXXX"       Bytes 60..63 inverted      base_snapshot_hash != snap
  Result: REJECTED          Result: REJECTED           Result: REJECTED
  Code: InvalidMagic        Code: HeaderCrcMismatch    Code: SnapshotMismatch
       │                          │                          │
       ├──────────────────────────┼──────────────────────────┘
       ▼                          ▼
[Corrupted Block Header]  [Corrupted Event Record]   [Truncated Block Stream]
  Block[3].crc32c ^= 0xFF   Block[5].record[2] bitflip Block truncated at byte 320
  Result: REJECTED          Result: REJECTED           Result: REJECTED
  Code: BlockCrcMismatch    Code: BlockCrcMismatch     Code: TruncatedBlock
```

### Snapshot Association Enforcement
When `require_snapshot_association = true` (default in Stage 21), the replay engine compares `WSndlHeader::base_snapshot_hash` with the target snapshot's CRC. In Test Group E, passing a valid SNDL paired with an unrelated valid snapshot was detected and rejected with `ReplayErrorCode::SnapshotMismatch`, preventing cross-log state corruption.
