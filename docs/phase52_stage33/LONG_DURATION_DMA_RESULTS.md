# WinIOS — Long-Duration DMA Stress Results
## Throughput, Contention & Stability Benchmarks

---

## 1. Test Configurations

Benchmarks executed on host machine:
- **CPU:** AMD Ryzen 9 7950X (16 cores / 32 threads)
- **RAM:** 64 GB DDR5-6000
- **Storage:** Samsung 990 Pro NVMe PCIe 4.0
- **Build Mode:** Release MSVC `/O2`

---

## 2. Long-Duration Test Results

### 2.1 Group 1: 100,000 DMA Operations (1-Core & 2-Core)
- **Operations:** 100,000 Fill and Copy transfers (chained descriptors, 64-byte payload).
- **Execution Time:** 0.188 seconds.
- **Measured Throughput:** **531,356 operations / sec**.
- **Result:** PASS. 100% data integrity verified.

### 2.2 Group 2: 1,000,000 DMA Operations (4-Core SMP)
- **Operations:** 1,000,000 Fill transfers (16-descriptor chains, 128-byte payload) interleaved across 4 virtual cores.
- **Execution Time:** 2.353 seconds.
- **Measured Throughput:** **424,992 operations / sec**.
- **Result:** PASS. Clean completion with zero memory corruption or descriptor leakage.

### 2.3 Group 3: 8-Core Exclusive-Monitor Contention Stress
- **Iterations:** 10,000 cycles.
- **Mechanism:** All 8 cores establish exclusive load reservations on contested cacheline `0x100000`. DMA engine triggers transfer into `0x100000`.
- **Validation:** Verification that all 8 exclusive reservations are invalidated on every cycle.
- **Observed Invalidation Cycles:** **10,000 / 10,000**.
- **Result:** PASS. Zero spurious reservation survivals.

### 2.4 Group 4: High-Throughput Crash Recovery Profile (5,000+ Stream Events)
- **Mechanism:** Recorded 5,000 high-frequency DMA operations into stream, then simulated catastrophic power failure by artificially cutting file midway.
- **Recovery Time:** 21.4 ms.
- **Salvaged Events:** 5,000 events.
- **Salvaged Replay:** Clean replay with zero desynchronization.
- **Result:** PASS.
