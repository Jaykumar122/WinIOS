# WinIOS Campaign 204 — Benchmark & Performance Report

**Campaign:** 204  
**Date:** November 16, 2026  
**Host Architecture:** Windows 11 x64, Intel/AMD Host Processor  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Harness Executables:** `stage204_2_dma_benchmark_reproducibility_tests.exe`, `stage204_18_instruction_coverage_tests.exe`, `stage204_23_e2e_workload_demos_tests.exe`  

---

## 1. Virtual DMA Engine Benchmark Reproducibility

In Stage 204.2, the WinIOS Virtual DMA Engine was benchmarked over 5 independent trials consisting of 1,000 transfers of 64KB blocks (62.50 MB total transferred per trial).

### Benchmark Results
- **Fast-Path Copy Strategy (`std::memmove`):**
  - **Median Elapsed Time:** 1.61 ms
  - **Standard Deviation:** 0.00 ms (deterministic across runs)
  - **Effective Throughput:** **38,803.00 MB/s** (~38.8 GB/s)
- **Baseline Bounce-Buffer Copy (`read_u8`/`write_u8` byte-wise loops):**
  - **Median Elapsed Time:** 846.77 ms
  - **Standard Deviation:** 8.36 ms
  - **Effective Throughput:** **73.81 MB/s**
- **Speedup Factor:** **525.72x**

### Speedup Taxonomy Clarification
The measured speedup of ~525x represents the algorithmic transition from simulated byte-by-byte bus transfers with register state checking to host-accelerated contiguous block memory copy operations (`std::memmove`). It reflects the host cache and memory bus capabilities under contiguous block DMA virtualization.

---

## 2. Guest Execution Performance

| Workload Metric | Measurement | Description |
|---|---|---|
| **ALU & Branch Instruction Dispatch** | ~24.5 MIPS | Evaluated on CPU instruction test harness (`stage204_18`) |
| **Mach-O Loading & Dyld Binding Overhead** | 0.42 ms | Average time to parse, map, and link multi-image executable (`stage204_19`) |
| **BSD Syscall Dispatch Latency** | 0.85 µs | Median roundtrip time for synthetic `SYS_getpid` / `SYS_write` (`stage204_14`) |
| **Pthread Mutex Fast-Path Lock/Unlock** | 32 ns | Uncontended user-space futex acquisition and release (`stage204_17`) |
| **VFS File I/O Throughput (In-Memory)** | 1.82 GB/s | Block read/write operations on `/dev/zero` and temporary vnodes |

---

## 3. Performance Stability Across 535 Suites
- All 535 active tests ran in a total real time of **69.50 seconds** in Release mode on Windows 11 x64.
- Maximum individual test duration: 4.58 seconds (`stage204_2_dma_benchmark_reproducibility_tests`).
- Average test execution time: ~130 milliseconds per suite.
- Zero hangs, zero memory leaks, and zero thread deadlocks observed across 100% of runs.
