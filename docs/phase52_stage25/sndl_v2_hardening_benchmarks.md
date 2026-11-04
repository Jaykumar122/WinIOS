# WinIOS — Phase 52, Stage 25: Performance & Microbenchmark Characterization

## 1. Test Environment
- **Platform**: Windows 11 Enterprise (x64)
- **Compiler**: MSVC Visual Studio 18 2026 (C++20, Release `/O2 /Ob2`)
- **Benchmark Suite**: `sndl_v2_hardening_benchmark.exe`

## 2. Microbenchmark Results

### Benchmark 1: Serialization Throughput (100,000 Records)
- **Direct Encode**: 6.38 ms | **15.67 M records/sec** | **1,195.42 MB/sec**
- **Decode**: 6.28 ms | **15.93 M records/sec** | **1,215.55 MB/sec**

### Benchmark 2: Block File I/O & Streaming Validation (50,000 Events)
- **Disk File Write**: 8.63 ms (3.82 MiB) | **442.42 MB/sec**
- **Streaming Validation**: 17.77 ms | **2.81 M events/sec** | **214.92 MB/sec**

### Benchmark 3: Scheduler Execution Overhead & Canonical Parity
- **2 Cores (50,000 instructions)**:
  - Base: 5.18 ms | Rec: 6.80 ms | Overhead: **31.3%** | Hash: **MATCH**
- **4 Cores (50,000 instructions)**:
  - Base: 5.36 ms | Rec: 6.79 ms | Overhead: **26.6%** | Hash: **MATCH**
- **8 Cores (50,000 instructions)**:
  - Base: 5.15 ms | Rec: 6.91 ms | Overhead: **34.3%** | Hash: **MATCH**
