# Multicore Replay Performance Profile & Analysis

## 1. Benchmarking Methodology
Empirical performance profiling was conducted using `smp_replay_performance_profile.exe` across 1, 2, 4, and 8 virtual cores under Release optimization on Windows x64.

## 2. Performance Metrics Table

| Cores | Retired Insns | Replay Time (ms) | Speed (MIPS) | Events Consumed | Event Rate (evt/s) | Snapshot Restore (ms) | Hash Compute (µs) | Peak RSS (MB) |
|---|---|---|---|---|---|---|---|---|
| 1 | 50,000 | 6.16 | 8.12 | 3,000 | 487,171 | 8.36 | 937.6 | 9 |
| 2 | 100,000 | 11.84 | 8.44 | 6,000 | 506,564 | 9.48 | 946.8 | 9 |
| 4 | 200,000 | 22.07 | 9.06 | 12,000 | 543,714 | 9.40 | 945.6 | 9 |
| 8 | 400,000 | 43.29 | 9.24 | 24,000 | 554,430 | 8.60 | 950.9 | 9 |

## 3. Analysis of Scaling Characteristics
1. **Linear Time Scaling**: As instruction count scales linearly with core count (50k for 1 core to 400k for 8 cores, an 8x increase), total replay time increases from 6.16 ms to 43.29 ms (~7.0x increase), confirming that scheduler overhead scales sublinearly.
2. **Instruction Throughput (MIPS)**: Execution throughput remains consistently above 8.0 MIPS across all topologies, peaking at 9.24 MIPS on 8 cores due to amortized block decoding efficiency.
3. **Event Consumption Throughput**: Event parsing throughput scales from 487,171 events/sec on 1 core to 554,430 events/sec on 8 cores.
4. **Cluster Hashing Overhead**: MurmurHash3-64 computation across all architectural registers, PC, system registers, and memory across all 8 cores completes in under 1.0 ms (~950.9 µs).
5. **Memory Footprint**: Peak resident set size remains flat at 9 MB across all topologies.
