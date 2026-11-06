# Large Snapshot & Memory Pressure Stress Analysis

## 1. Workload Profiles
Memory management and snapshot serialization were evaluated under two large RAM footprints:

### 1.1 Profile A: 16 MB Highly Compressible RAM
- Configuration: 16 MB allocated RAM across 8 cores. Memory initialized with repetitive patterns.
- Serialized `.wsnap v2` file size: 14,365 bytes (~14 KB).
- Snapshot capture latency: 10,370 microseconds (~10.4 ms).
- Snapshot restore latency: 18,968 microseconds (~19.0 ms).
- Peak resident set size (RSS): 21 MB.
- Cluster state hash match: Verified bit-for-bit identical before capture and after restore.

### 1.2 Profile B: 32 MB Sparse RAM
- Configuration: 32 MB allocated RAM across 8 cores. Non-zero pseudo-random pages written across sparse 64 KB strides.
- Serialized `.wsnap v2` file size: 2,115,613 bytes (~2.06 MB).
- Snapshot capture latency: 23,416 microseconds (~23.4 ms).
- Snapshot restore latency: 38,482 microseconds (~38.5 ms).
- Peak resident set size (RSS): 41 MB.
- Cluster state hash match: Verified bit-for-bit identical.

## 2. Repeated Restore & Replay Cycles
To verify memory deallocation integrity, 5 consecutive capture, restore, and replay cycles were performed back-to-back:
- Cycle 1 RSS: 41 MB
- Cycle 2 RSS: 41 MB
- Cycle 3 RSS: 41 MB
- Cycle 4 RSS: 41 MB
- Cycle 5 RSS: 41 MB

Observation: Resident memory remained flat at 41 MB without accumulation. No memory leaks detected.
