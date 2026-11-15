# WinIOS Campaign 199: DMA Isolation & SMP Interaction Report

**Subsystem:** DMA Engine IOVA Translation, Cross-Domain Isolation & Cache/Monitor Coherency  
**Target Architecture:** Apple A16 Bionic `T8120` / ARMv8.5-A  
**Classification:** `DMA_ISOLATION` / `SMP_COHERENCY`  
**Status:** FULLY IMPLEMENTED & VERIFIED  

---

## 1. Subsystem Architecture

In WinIOS Campaign 199, DMA engine operations were advanced from direct physical memory copying to IOMMU-mediated virtual DMA transfers. The `VirtualDmaEngine` (`src/device/virtual_dma_engine.h/cpp`) was tightly integrated with `VirtualIommu`:

1. **IOVA Translation in DMA Descriptor Processing:**
   - Every `DmaDescriptor` contains `src_addr` and `dst_addr` representing Input-Output Virtual Addresses (IOVAs).
   - If an IOMMU is attached to the DMA device, the DMA engine translates both addresses using the device's attached domain before copying bytes.
   - Translation faults abort the transfer immediately, set error status in the descriptor ring, and record fault metrics.
2. **Strict Cross-Domain Device Isolation:**
   - Devices attached to Domain 1 have zero visibility into physical memory pages mapped only in Domain 2.
   - Attempts to access unmapped or foreign IOVAs result in instant transaction termination.
3. **SMP Exclusive Monitor Invalidation:**
   - In accordance with the ARMv8-A architecture, when an external DMA master commits a write to physical memory, any CPU core holding an exclusive reservation overlapping that address range must have its reservation cleared.
   - `VirtualDmaEngine::submit_transfer` was wired to `SmpCluster::invalidate_all_monitors_for_address(pa, len)` across the translated destination address range.
4. **Completion Interrupt Routing:**
   - Upon completing a descriptor queue, `VirtualDmaEngine` asserts a configurable GICv3 Shared Peripheral Interrupt (SPI 48 / SPI 50).
   - The interrupt distributor routes the signal to the target CPU core according to `GICD_IROUTER` affinity settings.

---

## 2. Verification Evidence

The DMA isolation and SMP interaction subsystem was verified across four test suites:
- `stage199_5_dma_translation_tests`:
  - `test_dma_iova_to_pa_copy`: Verified successful transfer between non-contiguous IOVA ranges mapped to disjoint physical pages.
  - `test_dma_source_translation_fault`: Source IOVA fault properly terminates transfer and prevents destination corruption.
  - `test_dma_destination_translation_fault`: Destination IOVA fault properly terminates transfer.
- `stage199_6_device_isolation_tests`:
  - `test_cross_domain_isolation`: Device A (Domain 1) attempting to access Device B (Domain 2) buffer fails with translation fault.
  - `test_multi_device_shared_domain`: Devices attached to the same domain can access common buffers.
  - `test_unattached_device_rejection`: Unattached device cannot execute DMA transfers.
- `stage199_7_iommu_smp_integration_tests`:
  - `test_translated_dma_clears_exclusive_monitor`: CPU core executes `LDXR` on a memory address; DMA write to that address via IOVA translation invalidates the core's monitor; subsequent `STXR` returns status 1 (failure).
  - `test_translated_dma_non_interfering_monitor`: DMA write to unrelated address leaves exclusive monitor intact (`STXR` succeeds).
- `stage199_14_multicore_interrupt_tests`:
  - `test_dma_completion_interrupt_generation`: DMA queue completion asserts GICv3 SPI interrupt.
  - `test_gic_multicore_affinity_routing`: Interrupt routed specifically to Core 3 based on affinity configuration, leaving Core 0..2 undisturbed.
