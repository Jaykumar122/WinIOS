# Topology Scalability Benchmarks
## Quantitative Performance Analysis Across 1, 2, 4, and 8 Virtual Cores

---

## 1. Benchmark Execution Environment

- **Host OS**: Microsoft Windows 11
- **Toolchain**: MSVC (Visual Studio 2026), CMake
- **Target**: `smp_scalability_benchmark.exe` (Release Build)
- **Scheduling Model**: Single Host Thread, Deterministic Round-Robin
- **Instruction Quantum**: 1,000 instructions per slice

---

## 2. Benchmark Measurement Matrix

| Core Count | Retired Instructions | Execution Time (ms) | Throughput (MIPS) | Quantum Selections / sec | Snapshot Capture (ms) | Snapshot Restore (ms) |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **1 Core** | 10,000 | 0.91 ms | **11.04 MIPS** | 22,087 | 0.17 ms | 0.04 ms |
| **2 Cores** | 20,000 | 1.78 ms | **11.25 MIPS** | 22,495 | 0.16 ms | 0.03 ms |
| **4 Cores** | 40,000 | 3.52 ms | **11.38 MIPS** | 22,754 | 0.17 ms | 0.04 ms |
| **8 Cores** | 80,000 | 7.18 ms | **11.14 MIPS** | 22,289 | 0.20 ms | 0.05 ms |

---

## 3. Analysis & Key Observations

1. **Linear Instruction Scaling**:
   - Total execution time scales linearly with total instructions executed ($0.91\text{ ms} \to 1.78\text{ ms} \to 3.52\text{ ms} \to 7.18\text{ ms}$).
   - Overall interpreter throughput remains exceptionally stable between **11.04 and 11.38 MIPS**, showing near-zero overhead as core count scales.

2. **Scheduler Efficiency**:
   - Quantum selection throughput remains constant at ~22,000 selections/sec across all configurations, confirming $O(1)$ scheduler overhead per slice.

3. **Sub-Millisecond Snapshots**:
   - Even with 8 full architectural core states, `.wsnap v2` snapshot capture requires only **0.20 ms**, and restoration requires only **0.05 ms**.
