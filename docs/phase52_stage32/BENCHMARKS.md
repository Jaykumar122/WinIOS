# WinIOS — DMA Performance & Replay Benchmarks
## Throughput, Latency & Capture Overhead Analysis

---

## 1. Benchmark Configuration

- **Host Environment:** Windows x64, MSVC 2026 Release Build
- **Target Topology:** 4-Core SMP
- **Guest RAM:** 16 MiB
- **Workload:** 16 Chained Descriptors of 64 KiB each (1.0 MiB total transfer)
- **Operation:** Memory-to-Memory Virtual DMA Copy

---

## 2. Experimental Results

```
=================================================================
 WinIOS Phase 52 Stage 32: DMA Recording & Replay Benchmark
 Configuration: 4 Cores, 16 MiB RAM, 16 Chained Transfers x 64 KiB
=================================================================

[Direct Live DMA (Unrecorded)]
  Execution Time   : 12.93 ms
  Throughput       : 77.34 MB/s
  Latency/Desc     : 808.16 us

[Recorded Live DMA (SNDL v2 + Companion Payload Store)]
  Execution Time   : 15.35 ms
  Throughput       : 65.14 MB/s
  Capture Overhead : 18.7 %

[Deterministic Replay Execution (No Live Engine, No Source Reads)]
  Replay Time      : 15.07 ms
  Replay Throughput: 66.36 MB/s
  Events Consumed  : 20
  Status           : COMPLETED
=================================================================
```

---

## 3. Analysis

1. **Capture Overhead:** Recording introduces only **18.7%** execution overhead while capturing full pre-commit payloads, computing CRC32Cs, and streaming structured SNDL v2 records.
2. **Replay Efficiency:** Replay matches live execution performance (15.07 ms vs 15.35 ms), reproducing all memory mutations directly from the companion payload store with zero device engine emulation.
3. **Descriptor Latency:** Descriptor processing latency is bounded at ~800–960 us per 64 KiB transfer, including all validation and exclusive monitor invalidation stages.
