# Replay Scalability Results & Workload Analysis

## 1. Benchmarking Methodology & Testbed

All measurements were obtained on the reference execution platform:
- **CPU**: AMD Ryzen 5 4600H (6 cores / 12 hardware threads @ 3.0 GHz base, 4.0 GHz boost)
- **RAM**: 32 GB DDR4-3200 dual-channel
- **Storage**: Fast NVMe PCIe M.2 SSD
- **Host OS**: Microsoft Windows 11 Enterprise (x86-64)
- **Toolchain**: Microsoft Visual Studio 2026 MSVC v143 / v144, C++20, Release build (`/O2 /Oi /Gy /MD`)
- **Harness**: `replay_scalability_benchmark.exe` operating on parameterized synthetic workloads (`ReplayWorkload`).

Workloads utilize a deterministic 100-instruction loop:
$$\text{Loop} = [\text{MRS } X0, \text{CNTPCT\_EL0}] + [98 \times \text{NOP}] + [\text{B } \text{Loop}]$$
with an exact 1:100 ratio of nondeterministic input events (1 `CounterRead` record every 100 instructions). Baseline guest physical RAM is configured to 16 MB.

---

## 2. Empirical Scalability Matrix

The test harness executed five progressively scaled workloads from 100,000 to 100,000,000 instructions:

| Workload Tier | Instructions Retired | SNDL Records | Snapshot (`.wsnap`) | Input Log (`.sndl`) | Replay Time | Execution Speed | Event Rate | Checkpoints | Peak Working Set |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **Small** | 100,000 | 1,000 | 16.01 MB | 0.06 MB | 163.3 ms | **0.61 MIPS** | 6,120 /s | 2 | 52 MB |
| **Medium** | 1,000,000 | 10,000 | 16.01 MB | 0.61 MB | 295.8 ms | **3.38 MIPS** | 33,800 /s | 3 | 52 MB |
| **Large** | 10,000,000 | 100,000 | 16.01 MB | 6.11 MB | 1,632.7 ms | **6.12 MIPS** | 61,200 /s | 12 | 52 MB |
| **Very Large** | 50,000,000 | 500,000 | 16.01 MB | 30.54 MB | 7,568.1 ms | **6.61 MIPS** | 66,100 /s | 52 | 52 MB |
| **Stress** | 100,000,000 | 1,000,000 | 16.01 MB | 61.08 MB | 15,061.1 ms | **6.64 MIPS** | 66,400 /s | 102 | 65 MB |

### Pre-Replay Validation Latency
Before execution, both artifacts undergo structural header and CRC32C verification:
- **Snapshot Validation (`SnapshotManager::validate_file`)**: Strictly constant time (~23.8 to 25.8 ms for 16 MB) as memory chunk CRC32Cs are streamed and verified via slicing-by-8.
- **SNDL Validation (`SndlValidation::validate_file`)**: Strictly linear in log size:
  - 1k records (0.06 MB): 0.39 ms
  - 10k records (0.61 MB): 1.31 ms
  - 100k records (6.11 MB): 11.07 ms
  - 500k records (30.54 MB): 54.13 ms
  - 1M records (61.08 MB): 103.55 ms (validation throughput ~590 MB/s).

---

## 3. Scaling Curve Analysis

```
Throughput (MIPS)
 7.0 ┌─────────────────────────────────────────────────────────● 6.64 (100M)
     │                                           ● 6.61 (50M)
 6.0 ├─────────────────────────────● 6.12 (10M)
 5.0 │
 4.0 │
 3.0 ├──────────────● 3.38 (1M)
 2.0 │
 1.0 │
 0.0 └─● 0.61 (100k)
       100k         1M            10M           50M            100M
                             Instructions Retired
```

### Observations
1. **Startup Amortization**: At 100k instructions, one-time initialization costs (allocating 16 MB guest RAM, restoring the snapshot, opening files) dominate execution time. By 10M instructions, initialization overhead drops below 5%, allowing throughput to stabilize at ~6.1–6.6 MIPS.
2. **Streaming Invariance**: Execution time scales strictly linearly ($O(N)$) from 10M to 100M instructions (1.63 s $\rightarrow$ 7.57 s $\rightarrow$ 15.06 s).
3. **Memory Boundedness**: Process resident working set remains virtually flat: 52 MB across Small, Medium, Large, and Very Large, increasing slightly to 65 MB during the 100M-instruction stress test.
4. **Deterministic Completion**: All five workload tiers completed with status `CompletedNormally`, zero unconsumed events, and zero divergence.
