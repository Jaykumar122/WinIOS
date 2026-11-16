# WinIOS Campaign 201 — Stage 201.13 & 201.14 IOMMU & DMA Stress Validation

**Campaign:** 201  
**Stages:** 201.13 (IOMMU Domain Stress) & 201.14 (DMA Memory Safety)  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target:** Stress validation of Virtual IOMMU domain isolation, IOVA address translation, and DMA cache coherency  
**Date:** November 16, 2026  

---

## 1. Executive Summary

Stages 201.13 and 201.14 established the memory safety, fault atomicity, and multi-core coherency of the WinIOS Virtual IOMMU (DART) and DMA subsystems under high-volume stress testing.

Key validation conclusions:
1. **High-Volume Domain Lifecycle:** Validated rapid allocation, population, and teardown of 100 isolated IOMMU domains without memory leakage or state cross-contamination.
2. **Domain Isolation & Address Space Multiplexing:** Proved that identical IOVA addresses mapped in distinct domains resolve to completely isolated physical addresses. A write by Device A cannot pollute or access Device B's physical memory.
3. **Read-Only Permission Enforcement:** Devices attempting DMA writes to Read-Only IOVA mappings halt immediately with permission faults without writing a single byte to physical RAM (fault atomicity).
4. **CPU-DMA Cache Coherency:** Verified that physical memory writes performed by the asynchronous DMA engine broadcast invalidation events across all active SMP exclusive reservation monitors, causing subsequent CPU `store_conditional` operations on the affected 64-byte granule to fail predictably.

---

## 2. IOMMU & DMA Verification Matrix

| Subsystem Component | Test Scenario / Stress Pattern | Architectural Rule | WinIOS Observed Behavior | Status |
| :--- | :--- | :--- | :--- | :--- |
| **Domain Lifecycle** | 100 Domains created, mapped, destroyed | Clean allocation & destruction | 100 domains cycled, count returns to 0 | **PASS** |
| **IOVA Isolation** | Device A (Domain 1) & Device B (Domain 2) share IOVA `0x4000` | Translations must yield distinct PAs | Device A $\to$ `0x10004000`, Device B $\to$ `0x20004000` | **PASS** |
| **Write Protection** | Device B attempts DMA write to RO mapping | Translation must fail with `PermissionFault` | Translation fails, fault recorded | **PASS** |
| **Fault Atomicity** | DMA descriptor execution with RO destination | Target physical memory must not change | Destination data unaltered | **PASS** |
| **Detached Device** | Translation request on unattached Device ID | Fails with `DeviceNotAttached` | Trapped, out PA is 0 | **PASS** |
| **Unmapped Address** | Translation request on unmapped IOVA | Fails with `TranslationFault` | Trapped, fault recorded | **PASS** |
| **Exclusive Invalidation**| CPU load-linked, DMA write to same 64B granule | Monitor reservation invalidated | Reservation cleared, store-conditional fails | **PASS** |

---

## 3. Test Suite Reference
- Executable 1: `stage201_13_iommu_stress_tests`
- Executable 2: `stage201_14_dma_safety_tests`
