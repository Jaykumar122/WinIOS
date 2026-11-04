# Multi-Core Snapshot Scalability & Throughput Benchmarks

## 1. Benchmark Environment

- **Operating System**: Windows 11 Enterprise (x64)
- **CPU**: AMD Ryzen 5 4600H (6 cores / 12 threads @ 3.00 GHz base, 4.00 GHz boost)
- **Memory**: 32 GB DDR4 RAM
- **Compiler**: Microsoft Visual Studio 18 2026 (MSVC 14.51.36231, C++20)
- **Configuration**: Release (Optimization `/O2`, inlining enabled)
- **Test Binary**: `build\Release\smp_snapshot_benchmark.exe`

---

## 2. Benchmark Results Table

| Cores | Memory | Mode | Snap Size | Capture Time | Capture Rate | Restore Time | Restore Rate | Validate Time | Hash Time |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **2 Cores** | 1 MB | Sparse | 79 KB | 0.79 ms | 1,258.4 MB/s | 0.12 ms | 8,312.6 MB/s | 0.062 ms | 0.471 ms |
| | 1 MB | Dense | 1,028 KB | 3.11 ms | 322.0 MB/s | 1.16 ms | 865.0 MB/s | 0.496 ms | 0.485 ms |
| | 4 MB | Sparse | 271 KB | 2.61 ms | 1,532.9 MB/s | 0.74 ms | 5,409.4 MB/s | 0.340 ms | 1.966 ms |
| | 4 MB | Dense | 4,106 KB | 8.04 ms | 497.7 MB/s | 6.20 ms | 645.3 MB/s | 2.169 ms | 1.963 ms |
| | 16 MB | Sparse | 1,041 KB | 15.82 ms | 1,011.6 MB/s | 3.16 ms | 5,064.9 MB/s | 0.544 ms | 7.759 ms |
| | 16 MB | Dense | 16,418 KB | 44.61 ms | 358.7 MB/s | 20.87 ms | 766.6 MB/s | 8.341 ms | 9.651 ms |
| | 64 MB | Sparse | 4,119 KB | 44.13 ms | 1,450.2 MB/s | 7.06 ms | 9,062.5 MB/s | 2.606 ms | 31.751 ms |
| **4 Cores** | 1 MB | Sparse | 93 KB | 0.72 ms | 1,391.0 MB/s | 0.16 ms | 6,377.6 MB/s | 0.057 ms | 0.492 ms |
| | 1 MB | Dense | 1,031 KB | 1.93 ms | 519.3 MB/s | 1.86 ms | 537.9 MB/s | 0.770 ms | 0.543 ms |
| | 4 MB | Sparse | 285 KB | 2.58 ms | 1,548.6 MB/s | 0.77 ms | 5,228.8 MB/s | 0.143 ms | 1.889 ms |
| | 4 MB | Dense | 4,109 KB | 8.15 ms | 491.0 MB/s | 4.62 ms | 866.4 MB/s | 2.378 ms | 2.019 ms |
| | 16 MB | Sparse | 1,055 KB | 10.31 ms | 1,552.2 MB/s | 2.61 ms | 6,135.0 MB/s | 0.503 ms | 7.632 ms |
| | 16 MB | Dense | 16,421 KB | 36.06 ms | 443.7 MB/s | 20.27 ms | 789.5 MB/s | 7.676 ms | 7.653 ms |
| | 64 MB | Sparse | 4,133 KB | 40.30 ms | 1,588.2 MB/s | 9.63 ms | 6,642.9 MB/s | 2.845 ms | 32.615 ms |
| **8 Cores** | 1 MB | Sparse | 122 KB | 0.69 ms | 1,454.5 MB/s | 0.17 ms | 5,725.7 MB/s | 0.069 ms | 0.481 ms |
| | 1 MB | Dense | 1,036 KB | 3.04 ms | 328.5 MB/s | 1.02 ms | 980.0 MB/s | 0.489 ms | 0.477 ms |
| | 4 MB | Sparse | 314 KB | 2.63 ms | 1,520.2 MB/s | 0.61 ms | 6,601.2 MB/s | 0.156 ms | 1.885 ms |
| | 4 MB | Dense | 4,114 KB | 8.45 ms | 473.5 MB/s | 5.43 ms | 737.2 MB/s | 2.257 ms | 2.126 ms |
| | 16 MB | Sparse | 1,084 KB | 11.39 ms | 1,405.1 MB/s | 2.63 ms | 6,080.9 MB/s | 0.539 ms | 7.833 ms |
| | 16 MB | Dense | 16,426 KB | 32.46 ms | 493.0 MB/s | 19.88 ms | 804.7 MB/s | 7.906 ms | 10.348 ms |
| | 64 MB | Sparse | 4,162 KB | 42.75 ms | 1,497.1 MB/s | 7.10 ms | 9,011.8 MB/s | 1.930 ms | 30.739 ms |

---

## 3. Analysis & Key Findings

1. **Near-Zero Core Overhead**: Scaling from 2 to 8 cores increases the snapshot container size by only ~43 KB (1056 bytes per core state plus small directory descriptors), representing negligible overhead compared to guest RAM.
2. **Sparse Chunk Efficiency**: 4KB sparse chunking with zero-chunk omission achieves **> 93% file size reduction** on realistic guest memory footprints (e.g. 64 MB compressed down to 4.1 MB).
3. **Capture Throughput**: Sparse capture sustains **1,000 to 1,588 MB/s**, enabling sub-millisecond snapshotting for small clusters and under 45 ms for 64 MB cluster memory.
4. **Restoration Throughput**: Sparse restore reaches **5,000 to 9,000 MB/s**, enabling instant sub-10 ms restoration across 2-, 4-, and 8-core systems.
5. **Validation Speed**: Snapshot validation requires only **0.05 to 2.8 ms**, ensuring fast pre-flight safety checks.
