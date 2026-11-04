# Large Snapshot Validation & Structural Integrity Analysis

## 1. Architectural Snapshot Schema (`.wsnap`)

The WinIOS architectural snapshot engine (`.wsnap`) serializes the complete architectural state of the virtual machine into an immutable, versioned binary container:
- **Container Header (64 bytes)**: Magic (`WSNAP\0\0\0`), version, flags, total file size, section count, and 32-bit CRC32C over the first 60 header bytes.
- **Section Table**: Describes discrete subsystem payloads (CPU, Memory, VirtualTime, GICv3, PMU).
- **Memory Section**: Encodes guest physical RAM as 4 KB chunks with dirty-page compression and per-chunk CRC32C integrity checksums.

---

## 2. Large Snapshot Test Matrix

The Stage 21 test suite (`replay_scalability_tests.exe`, Test Group E) and benchmark suite (`replay_scalability_benchmark.exe`) evaluated snapshots across four orders of magnitude:

| Snapshot Category | Memory Allocation | File Size | Section Count | Non-Zero Chunks | Validation Latency | Status |
|---|---:|---:|---:|---:|---:|:---:|
| **Small Baseline** | 64 KB | 65.4 KB | 4 | 16 | 0.08 ms | **VALID** |
| **Medium VM** | 1 MB | 1.00 MB | 4 | 256 | 1.52 ms | **VALID** |
| **Standard VM** | 16 MB | 16.01 MB | 4 | 4,096 | 24.84 ms | **VALID** |
| **Large VM** | 64 MB | 64.02 MB | 4 | 16,384 | 99.41 ms | **VALID** |

*Validation throughput remains stable at ~650 MB/s across all snapshot sizes.*

---

## 3. Adversarial Corruption & Truncation Testing (Test Group E)

To prove fault tolerance, adversarial corruptions were systematically applied to temporary snapshot copies:

```
                  Valid Baseline Snapshot (16.01 MB)
                                   │
       ┌───────────────────────────┼───────────────────────────┐
       ▼                           ▼                           ▼
[Corrupted Magic]          [Bad Header CRC]            [Truncated File]
  Byte 0..3 = 0xDEADBEEF     Byte 60..63 flipped         File truncated to 64 B
  Result: REJECTED           Result: REJECTED            Result: REJECTED
  Code: InvalidMagic         Code: HeaderCrcMismatch     Code: TruncatedFile
       │                           │                           │
       ├───────────────────────────┼───────────────────────────┘
       ▼                           ▼
[Corrupted Section Offset] [Corrupted Memory Chunk]
  Section[1].offset = 0xFFF  Chunk[0].data[10] ^= 0x55
  Result: REJECTED           Result: REJECTED
  Code: InvalidSectionOffset Code: ChunkCrcMismatch
```

### Safety and Isolation Guarantee
In strict adherence to repository protection rules, **all corruption tests operate exclusively on temporary file copies (`test_snap_corrupt_*.wsnap`)**. Original baseline snapshots are never modified, overwritten, or truncated.
