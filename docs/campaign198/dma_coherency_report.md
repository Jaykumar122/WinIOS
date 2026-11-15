# WinIOS Campaign 198: Stage 198.9 DMA Coherency & Visibility Report

**Date:** 2026-11-15  
**Campaign:** 198 — ARM64 Memory-System Integration, Cache Coherency, DMA Consistency & SMP Virtual Platform Conformance  
**Target Architecture:** ARMv8-A / ARM64e virtual platform  
**Reservation Granule:** 64 bytes  
**Memory Model:** ARMv8-A Multi-Copy Atomic Normal Memory  

---

## 1. Overview of CPU-DMA Coherency Architecture

In the WinIOS virtual platform, the CPU cores and the virtual DMA engine (`VirtualDmaEngine`) access shared physical RAM backed by `SmpSharedMemory` and `Memory`.

Because CPU cores execute in their private L1 data/instruction cache domains, transfers between CPU and DMA devices require strict adherence to ARMv8 Point of Coherency (PoC) protocols:

1. **CPU Transmit Path (CPU Write -> DMA Read):**
   - Software writes payload into guest RAM.
   - Software cleans the cache lines to Point of Coherency: `DC CVAC, Xt` (or `DC CIVAC, Xt`).
   - Software issues a Data Synchronization Barrier: `DSB ISH`.
   - Software submits descriptor chain to `VirtualDmaEngine`.
   - DMA engine reads memory directly from physical RAM, observing the committed CPU data.

2. **CPU Receive Path (DMA Write -> CPU Read):**
   - DMA engine executes transfer, writing payload bytes into guest RAM.
   - Prior to reading, CPU executes `DC CIVAC, Xt` (or `DC IVAC, Xt`) on destination buffers.
   - CPU issues `DSB ISH`.
   - CPU reads payload from memory, observing the updated DMA bytes without stale cache line hazards.

---

## 2. Exclusive Reservation Invalidation Protocol

ARMv8-A multi-copy atomicity and exclusive monitor semantics require that any store from any memory observer (including non-CPU DMA bus masters) to a reservation granule must invalidate active reservations on all cores:

- **Granule Size:** 64 bytes (`kDefaultGranuleSize = 64`).
- **Hook Integration:** `SmpSharedMemory::write_bytes` dispatches to `SmpExclusiveMonitorManager::invalidate_reservations(writer_core_id, address, size)`.
- **Outcome:**
  - If Core $K$ has executed `LDXR` on address $A$, and a DMA write occurs covering any byte within the 64-byte granule of $A$, Core $K$'s exclusive reservation flag is immediately cleared.
  - A subsequent `STXR` on Core $K$ is guaranteed to return status 1 (failure) and commit no memory update.
  - DMA writes to disjoint 64-byte granules do not affect Core $K$'s reservation.

---

## 3. Verification Summary

The architectural CPU-DMA visibility and reservation invalidation behavior was verified in `tests/unit/stage198_9_dma_visibility_tests.cpp`:
- `test_cpu_write_dc_clean_dma_read`: Passed.
- `test_dma_write_cpu_invalidate_read`: Passed.
- `test_dma_exclusive_reservation_invalidation`: Passed.
- `test_dma_non_overlapping_reservation_preserved`: Passed.
