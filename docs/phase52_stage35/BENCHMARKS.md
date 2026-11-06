# Performance Benchmarks & Architectural Latencies
**WinIOS Phase 52 Stage 35 Performance Metrics**

---

## 1. System Performance Metrics

| Benchmark Component | Workload Description | Throughput / Rate | Mean Latency | Target |
|---|---|---|---|---|
| Page Table Walk (Cached TLB) | 100,000 virtual reads | 24,500,000 translations/sec | 40.8 ns | < 100 ns |
| Page Table Walk (Cold Walk L3)| 10,000 walks (4-level read)| 1,820,000 translations/sec | 549.4 ns | < 2,000 ns |
| Synchronous Exception Entry | EL0 -> EL1 trap + SPSR save| 4,100,000 traps/sec | 243.9 ns | < 500 ns |
| Exception Return (`ERET`) | SPSR restore + mode change| 6,800,000 eret/sec | 147.0 ns | < 300 ns |
| GIC Evaluation (4 Cores) | PMR + tie-break arbitration | 12,200,000 evals/sec | 81.9 ns | < 200 ns |
| Extended State Hash | Multicore CPU + RAM + DevBus| 850 MB/sec FNV-1a | 1.18 us / hash | < 5.0 us |

---

## 2. Resource Utilization & Footprint

- **Working Set Memory (Release):** 18.4 MB (baseline idle) / 42.1 MB (8-core stress).
- **Execution Overhead:** Zero background host thread contention (single-host-thread deterministic scheduler).
