# Phase 52, Stage 21: Comprehensive Performance & Scalability Benchmark Report

## 1. Benchmarking Environment Specifications

- **Host Operating System**: Microsoft Windows 11 Enterprise (64-bit, Build 22631)
- **Host Processor**: AMD Ryzen 5 4600H with Radeon Graphics (6 physical cores, 12 logical threads, base clock 3.0 GHz, max boost clock 4.0 GHz)
- **Host Memory**: 32.0 GB DDR4-3200 dual-channel RAM
- **Host Storage**: 512 GB PCIe NVMe M.2 SSD
- **Compiler**: Microsoft Visual C++ Compiler 19.44 (Visual Studio 2026 MSVC v143 / v144)
- **Compilation Flags**: `/O2 /Oi /Gy /MD /std:c++20 /permissive-` (Release configuration)
- **Benchmark Target**: `D:\WinISO26\WinIOS\build\Release\replay_scalability_benchmark.exe`

---

## 2. Benchmark Suite 1: Replay Scalability Matrix

Evaluated on a 16.01 MB baseline snapshot with a 1,000,000-instruction checkpoint interval:

| Tier | Instructions | Records | WSNAP (MB) | SNDL (MB) | Gen (ms) | SnapVal (ms) | SndlVal (ms) | Replay (ms) | MIPS | Events/s | Checkpoints | Peak WS (MB) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **Small** | 100,000 | 1,000 | 16.01 | 0.06 | 116.0 | 24.84 | 0.39 | 163.3 | **0.61** | 6,120 | 2 | 52 |
| **Medium** | 1,000,000 | 10,000 | 16.01 | 0.61 | 114.7 | 23.81 | 1.31 | 295.8 | **3.38** | 33,800 | 3 | 52 |
| **Large** | 10,000,000 | 100,000 | 16.01 | 6.11 | 118.6 | 24.42 | 11.07 | 1,632.7 | **6.12** | 61,200 | 12 | 52 |
| **Very Large** | 50,000,000 | 500,000 | 16.01 | 30.54 | 161.2 | 25.86 | 54.13 | 7,568.1 | **6.61** | 66,100 | 52 | 52 |
| **Stress** | 100,000,000 | 1,000,000 | 16.01 | 61.08 | 212.3 | 24.82 | 103.55 | 15,061.1 | **6.64** | 66,400 | 102 | 65 |

---

## 3. Benchmark Suite 2: Checkpoint Interval Sweep

Evaluated across 1,000,000 instructions on a 16.01 MB snapshot:

| Interval | Checkpoints | Replay Duration | Replay Throughput | Checkpoint Time | Memory Hash Time | Memory Hash Rate | Final Combined Hash |
|---|---:|---:|---:|---:|---:|---:|:---:|
| **0 (Disabled)** | 2 | 374.8 ms | **2.67 MIPS** | 31.2 ms | 46.3 ms | 2,071.74 MB/s | `0x0DB69FF394F4361E` |
| **100,000** | 12 | 557.9 ms | **1.79 MIPS** | 193.4 ms | 208.6 ms | 1,994.68 MB/s | `0x0DB69FF394F4361E` |
| **250,000** | 6 | 443.2 ms | **2.26 MIPS** | 91.3 ms | 106.5 ms | 2,103.77 MB/s | `0x0DB69FF394F4361E` |
| **500,000** | 4 | 404.0 ms | **2.47 MIPS** | 60.7 ms | 75.7 ms | 2,113.99 MB/s | `0x0DB69FF394F4361E` |
| **1,000,000** | 3 | 390.1 ms | **2.56 MIPS** | 46.1 ms | 61.4 ms | 2,084.18 MB/s | `0x0DB69FF394F4361E` |
| **2,000,000** | 2 | 379.9 ms | **2.63 MIPS** | 30.1 ms | 45.4 ms | 2,113.45 MB/s | `0x0DB69FF394F4361E` |

---

## 4. Benchmark Suite 3: Opt-in Profiling Overhead

Evaluated across 11 consecutive 1,000,000-instruction executions (1 warmup + 5 unprofiled + 5 profiled):

- **Profiling Disabled Execution Duration**:
  - Run 1: 441.20 ms
  - Run 2: 440.61 ms (median)
  - Run 3: 432.02 ms (min)
  - Run 4: 442.80 ms
  - Run 5: 440.11 ms
- **Profiling Enabled Execution Duration**:
  - Run 1: 531.10 ms
  - Run 2: 530.46 ms (median)
  - Run 3: 527.77 ms (min)
  - Run 4: 532.80 ms
  - Run 5: 529.90 ms
- **Measured Instrumentation Overhead**: **+20.39%**
- **State Hash Equivalence**: **100% Bit-for-bit identical** (`0x0DB69FF394F4361E`) across all 11 runs.

### Granular Phase Breakdown (Profiled Run)
- `prepare_total`: 102.05 ms (1 call)
- `run_total`: 427.59 ms (1 call)
- `cpu_execution`: 130.24 ms (1,000,000 calls @ 130.2 ns/inst)
- `checkpoint_total`: 185.67 ms (12 calls @ 15.47 ms/checkpoint)
- `interrupt_injection`: 27.85 ms (1,000,000 calls @ 27.9 ns/call)
- `final_hash`: 15.19 ms (1 call)
- `input_routing`: 1.59 ms (10,000 calls @ 158.7 ns/call)
- `cursor_block_load`: 1.41 ms (11 calls @ 127.9 µs/call)

---

## 5. Benchmark Suite 4: Guest Memory Scaling

Evaluated on 100,000 instructions with a 50,000-instruction checkpoint interval:

| Guest RAM | Snapshot Size | Replay Duration | Checkpoints | Memory Hash Time | Hashing Throughput | Peak Process WS |
|---|---|---|---|---|---|---|
| **1 MB** | 1.00 MB | 40.8 ms | 4 | 4.68 ms | 2,135.57 MB/s | 65 MB |
| **4 MB** | 4.00 MB | 68.7 ms | 4 | 18.95 ms | 2,110.52 MB/s | 65 MB |
| **16 MB** | 16.01 MB | 201.7 ms | 4 | 77.34 ms | 2,068.75 MB/s | 65 MB |
| **64 MB** | 64.02 MB | 734.5 ms | 4 | 303.98 ms | 2,105.41 MB/s | 196 MB |

---

## 6. Benchmark Suite 5: CRC32C Throughput (Slicing-by-8 vs. Bitwise)

Evaluated across 32 MB of memory data (16 MB buffer, double-hash chain):

- **Bitwise Reference**: 349.63 ms (Throughput: **91.53 MB/s**)
- **Slicing-by-8**: 15.65 ms (Throughput: **2,044.53 MB/s**)
- **Speedup Factor**: **22.34x**
- **Checksum Verification**:
  - $C_1 = \mathbf{0x58C1B679}$ (Bitwise and Slicing-by-8 match bit-for-bit)
  - $C_2 = \mathbf{0x14F6E860}$ (Bitwise and Slicing-by-8 match bit-for-bit)

---

## 7. Benchmark Suite 6: Process Memory Lifecycle Report

Evaluated across 1,000,000 instructions with a 1,000,000-instruction checkpoint interval:

- **Working Set Before Session**: 4,300,800 bytes (4.1 MB)
- **Working Set After Replay**: 20,971,520 bytes (20.0 MB)
- **Working Set Delta**: +16,670,720 bytes (+15.9 MB)
- **Private Bytes Before Session**: 684,032 bytes (0.7 MB)
- **Private Bytes After Replay**: 17,661,952 bytes (16.8 MB)
- **Private Bytes Delta**: +16,977,920 bytes (+16.1 MB)
- **Process Peak Working Set**: 196 MB
- **OS Handle Delta**: 48 handles $\rightarrow$ 49 handles (0 leak)
