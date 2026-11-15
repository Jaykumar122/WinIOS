# WinIOS Campaign 200: DMA End-to-End Validation Report

**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`) Virtual Platform  
**Stage:** 200.11  
**Status:** VALIDATED  
**Evidence Classification:** `VIRTUAL_IOMMU` & `DMA_ISOLATION`  

---

## 1. Executive Summary

This report documents the end-to-end integration and verification of the Virtual IOMMU and DMA subsystem under Campaign 200. The architecture guarantees deterministic, isolated, and coherent direct memory access across 8 simulated Apple A16 Bionic CPU cores and attached virtual peripherals.

The validation covers:
1. **Domain Lifecycle Management:** Dynamic domain allocation, multiple device attachments, detachment, and leak-free destruction.
2. **IOVA-to-PA Address Translation:** Fine-grained page mapping (16KB granule), permission enforcement (Read/Write, Read-Only), and out-of-bounds boundary safety.
3. **Fault Propagation Semantics:** Deterministic delivery of `UnmappedIova`, `PermissionDenied`, and `OutOfBounds` faults with complete diagnostic metadata (Device ID, Domain ID, IOVA, access type).
4. **SMP Cache and Memory Coherency:** Bidirectional visibility between CPU stores and DMA reads/writes, coupled with automatic invalidation of CPU exclusive reservations upon DMA writes.
5. **Interrupt Controller Integration:** GICv3 SPI interrupt signaling upon descriptor completion (`kDmaFlagInterruptOnComplete`) and CPU acknowledgment/EOI processing.

---

## 2. Test Matrix and Verification Results

The test suite consists of four dedicated unit test suites covering Stages 200.8 through 200.11:

| Stage | Test Suite | Description | Status |
|---|---|---|---|
| **200.8** | `stage200_8_iommu_lifecycle_tests` | Domain creation, duplicate rejection, multi-device attachment, detachment, teardown, and IOTLB flush. | PASS |
| **200.9** | `stage200_9_iova_fault_tests` | Valid IOVA translation, unmapped IOVA fault, write to RO IOVA, out-of-bounds limit check, and DMA engine abort on fault. | PASS |
| **200.10** | `stage200_10_dma_coherency_tests` | Multi-domain device isolation, bidirectional CPU-DMA coherency, exclusive monitor invalidation on DMA writes. | PASS |
| **200.11** | `stage200_11_dma_interrupt_tests` | DMA completion interrupt generation on GICv3 SPI 48, interrupt masking, acknowledgment, and deassertion lifecycle. | PASS |

---

## 3. Subsystem Architecture & Architectural Conformance

### 3.1 Domain Isolation Guarantees
Each peripheral device attached to `VirtualIommu` is bound to exactly one `IommuDomain`. Memory allocated to Domain $A$ is completely inaccessible to devices in Domain $B$. Attempting to access an IOVA belonging to an alternate domain generates an immediate `IommuFaultType::UnmappedIova` fault without causing memory corruption or data leakage across domains.

### 3.2 SMP Exclusive Monitor Invalidation
To prevent race conditions and maintain the ARM64 exclusive memory model:
- The CPU cluster maintains a 64-byte reservation granule for `LDXR` / `STXR`.
- When `VirtualDmaEngine` performs physical memory writes via `SmpSharedMemory::write_bytes`, any overlapping exclusive reservation held by any virtual CPU core is atomically invalidated.
- Unrelated reservations outside the written 64-byte cache line remain valid.

### 3.3 Interrupt Signaling Semantics
- When the descriptor has `kDmaFlagInterruptOnComplete` set, `VirtualDmaEngine::trigger_completion_irq()` asserts the configured SPI line (default INTID 48) in GICv3 (`set_spi_level(48, true)`).
- GICv3 evaluates pending signals according to `ICC_PMR_EL1` priority masks and CPU interface enable bits.
- Following CPU acknowledgment (`acknowledge_group1`) and DMA acknowledgment (`acknowledge_completion_irq`), the SPI line is deasserted (`set_spi_level(48, false)`) and cleared via EOI (`end_of_interrupt_group1`).

---

## 4. Conclusion

Phase C verification confirms that the WinIOS Virtual IOMMU and DMA architecture meets all security, isolation, and architectural coherency requirements for Apple A16 Bionic peripheral emulation.
