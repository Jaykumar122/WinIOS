# WinIOS — Known Limitations & Future Enhancements
## Current Constraints and Phase 52 Stage 34 Roadmap

---

## 1. Current Architectural Scope (Stage 33)

1. **Virtual DMA Channels:** Up to 32 independent DMA channels supported per virtual controller.
2. **Synchronous Scheduling:** Virtual DMA transfers execute synchronously on virtual core boundary points. True background asynchronous host worker threads for DMA are intentionally not permitted in order to preserve bit-for-bit determinism.
3. **Descriptor Chain Depth:** Maximum chained descriptor depth is bounded to 256 descriptors per trigger to prevent infinite loops from malicious circular descriptors.
4. **Memory Region Bounding:** DMA transfers are restricted to physical RAM address boundaries registered in `SmpSharedMemory`. Host memory or unmapped addresses trigger immediate descriptor faults.

---

## 2. Active Replay Safety Blockers Status

| Blocker | Description | Severity | Status |
|---|---|---|---|
| **BLK-SMP-01** | Multicore scheduler coordination | CRITICAL | **RESOLVED** (Stage 26) |
| **BLK-SMP-02** | Global and per-core retirement synchronization | CRITICAL | **RESOLVED** (Stage 26) |
| **BLK-SMP-03** | Asynchronous host DMA capture & replay | HIGH | **RESOLVED** (Stage 32/33) |
| **BLK-SMP-04** | Host wall-clock reads virtualization | HIGH | **RESOLVED** (Stage 27) |

All primary multi-core recording and replay safety blockers are now **RESOLVED**.

---

## 3. Recommendations for Stage 34

With crash consistency, companion payload integrity, and deterministic recovery proven:
1. **Multi-Queue Virtual Network Devices:** Introduce a virtio-net or synthetic NIC architecture built on the VirtualDeviceBus and VirtualDMA foundation.
2. **Scatter-Gather Page Lists:** Support non-contiguous guest physical page tables for zero-copy DMA buffers.
3. **Advanced Checkpoint Compaction:** Apply LZ4 or Zstandard delta compression to periodic `SmpReplayCheckpoint` images.
