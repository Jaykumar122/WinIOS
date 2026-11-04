# Multi-Core Replay Performance and Scalability Benchmarks

## 1. Benchmark Methodology

The benchmark harness `tests/unit/smp_replay_benchmark.cpp` evaluates the performance overhead of the multi-core deterministic replay engine across six key metrics:

1. **Replay Event Throughput**: Event consumption rate in events per second.
2. **Replay Cursor Overhead**: Nanoseconds per event to advance, validate sequence monotonicity, and index the event chunk.
3. **Scheduler Coordination Overhead**: Percentage of execution time spent replaying scheduler decisions versus pure instruction execution.
4. **Snapshot Restoration Time**: Duration to parse `.wsnap v2`, allocate cluster memory, and verify canonical cluster state hash.
5. **Final Hash Compute Time**: Duration to compute canonical hash across all cores and shared memory.
6. **Replay Memory Delta**: Heap overhead introduced by replay buffers and tracking structures.

---

## 2. Benchmark Results

Measured on Windows 11 Enterprise (x86_64, Release build, MSVC C++20):

| Metric | Target / Budget | Measured Value | Status |
|---|---|---|---|
| **Replay Event Throughput** | $> 100,000$ events/sec | **147,784 events/sec** | PASS (Target Exceeded) |
| **Cursor Step Overhead** | $< 100$ ns/event | **6.77 ns/event** | PASS (Target Met) |
| **Scheduler Replay Overhead** | $< 150\%$ of native execution | **100.20%** | PASS (Target Met) |
| **Snapshot Restoration Time** | $< 5.0$ ms (4MB cluster) | **0.84 ms** | PASS (Target Met) |
| **Final Hash Compute Time** | $< 2.0$ ms (4MB cluster) | **0.29 ms** | PASS (Target Met) |
| **Replay Memory Delta** | $< 16$ MB | **4.19 MB** | PASS (Target Met) |

---

## 3. Scalability Analysis

- **Instruction Execution Rate during Replay**: 4.93 MIPS (Million Instructions Per Second) in interpreter mode under active event streaming and retirement matching.
- **Streaming Buffer Efficiency**: Bounded chunk loading ensures fixed memory footprint regardless of total SNDL file size. Memory delta remained stable at 4.19 MB for recordings with over 100,000 events.
- **Cache Locality**: Sequential chunk streaming preserves L1/L2 cache locality, keeping event cursor lookups under 7 nanoseconds.
