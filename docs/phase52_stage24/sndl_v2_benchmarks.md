# WinIOS — Phase 52, Stage 24: Benchmark & Performance Characterization

## 1. Benchmark Environment
- **Host CPU**: AMD Ryzen / Intel x86_64, Windows 11 Enterprise
- **Build Mode**: Release (`/O2 /Ob2 /DNDEBUG`)
- **Compiler**: MSVC Visual Studio 18 2026 (C++20)
- **Suite**: `smp_sndl_v2_benchmark.exe`

## 2. Benchmark Results

### Benchmark 1: Serialization Throughput (100,000 Records)
- **Encode**:
  - Time: 6.438 ms
  - Throughput: **15.53 M records/sec** (**1,185.05 MB/sec**)
- **Decode**:
  - Time: 6.159 ms
  - Throughput: **16.24 M records/sec** (**1,238.83 MB/sec**)

### Benchmark 2: Streaming Block File Writing (100,000 Records)
- Total Written: 8.05 MB across 98 blocks
- Elapsed Time: 20.14 ms
- Disk Throughput: **399.60 MB/sec**
- Recording Rate: **4.97 M records/sec**

### Benchmark 3: Streaming Validation (100,000 Records)
- Elapsed Time: 43.60 ms
- Validation Rate: **2.29 M records/sec** (**175.30 MB/sec**)

### Benchmark 4: Multi-Core Scheduling Overhead Under Recording
Measured over 50,000 cycles per core across varying core counts:

| Core Count | Unrecorded Execution Time | Recorded Execution Time | Overhead % | Delta per Instruction |
|---|---|---|---|---|
| **2 Cores** | 9.09 ms | 12.39 ms | **+36.3%** | ~33.0 ns |
| **4 Cores** | 18.06 ms | 24.64 ms | **+36.4%** | ~32.9 ns |
| **8 Cores** | 35.83 ms | 50.41 ms | **+40.7%** | ~36.5 ns |

### Benchmark 5: Canonical Multi-Core State Parity
Across all core configurations (2, 4, and 8 cores):
- Canonical Multi-Core State Hash (Unrecorded): Match
- Canonical Multi-Core State Hash (Recorded): Match
- Divergence: **0 bytes (100% Bit-for-Bit Identical)**
