# Four-Core Replay Performance and Scalability Benchmarks

## 1. Benchmark Harness and Methodology

The benchmark target `tests/unit/smp_four_core_replay_benchmark.cpp` evaluates the performance scaling of 4-core replay on Windows 11 Enterprise (Release build, MSVC C++20).

### Workload Parameters
- Cores: 4.
- Shared RAM: 4 MB.
- Quantum Budget ($Q$): 100 instructions.
- Workload: 40,000 instructions retired across 4 cores (10,000 instructions/core).
- Recorded Events: 1,200 SNDL v2 records.

---

## 2. Benchmark Measurement Summary

| Metric | Target / Budget | Measured 4-Core Result | Measured 2-Core Baseline | Status |
|---|---|---|---|---|
| **Event Throughput** | $> 100,000$ events/s | **184,020 events/s** | 147,784 events/s | PASS |
| **Replay Execution Speed** | $> 4.0$ MIPS | **6.13 MIPS** | 4.93 MIPS | PASS |
| **Scheduler Coordination Overhead** | $< 100\%$ overhead | **62.58 %** | 100.20 % | PASS |
| **Snapshot Restore Time (4MB cluster)** | $< 15.0$ ms | **9.01 ms** | 0.84 ms (2-core) | PASS |
| **Final Hash Compute Time** | $< 5.0$ ms | **1.91 ms** | 0.29 ms (2-core) | PASS |
| **Replay Memory Delta (Heap RSS)** | $< 16.0$ MB | **4.12 MB** | 4.19 MB | PASS |
| **Native Execution Time** | — | 4.01 ms | 3.20 ms | Baseline |
| **Recording Execution Time** | — | 5.94 ms | 4.80 ms | Baseline |
| **Replay Execution Time** | — | 6.52 ms | 6.41 ms | Baseline |

---

## 3. Scalability Analysis
- Throughput scaled from 147.7k events/sec (2 cores) to 184.0k events/sec (4 cores) due to improved amortized streaming chunk efficiency.
- Scheduler overhead decreased to 62.58% due to longer contiguous quantum runs and efficient round-robin stepping.
- Memory consumption remained low and stable at 4.12 MB, well within the 16 MB budget.
