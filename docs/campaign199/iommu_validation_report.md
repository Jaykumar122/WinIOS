# WinIOS Campaign 199: Virtual IOMMU Validation Report

**Subsystem:** Virtual IOMMU & Apple DART (Device Address Resolution Table) Emulation  
**Target Architecture:** Apple A16 Bionic `T8120` / ARM SMMUv3  
**Classification:** `VIRTUAL_IOMMU`  
**Status:** FULLY IMPLEMENTED & VERIFIED  

---

## 1. Architectural Architecture

In modern Apple Silicon SoC platforms (including the A16 Bionic `T8120`), peripheral devices do not access physical memory directly. Instead, all memory transactions initiated by DMA engines, NVMe controllers, display pipelines, and audio subsystems are gated and translated by Device Address Resolution Tables (DARTs), Apple's proprietary IOMMUs.

Campaign 199 implemented the `VirtualIommu` framework in `src/device/virtual_iommu.h/cpp`:
- **Domain-Based Protection:** Each IOMMU domain (`IommuDomain`) maintains an independent page table hierarchy mapping 64-bit Input-Output Virtual Addresses (IOVAs) to Physical Addresses (PAs).
- **Device Attachment:** Devices are assigned unique 32-bit `DeviceId`s. Each device attaches to exactly one domain; unattached devices are denied DMA access.
- **Granular Permissions:** Every mapped IOVA page possesses independent `Read`, `Write`, and `Privileged` access attributes (`IommuAccess`).
- **Translation Lookaside Buffer (TLB):** Low-latency IOVA caching per domain, with support for domain-wide and page-specific invalidation (`invalidate_domain_tlb`, `invalidate_page_tlb`).
- **Thread Safety:** Fully synchronized domain management and translation pipeline utilizing recursive mutex synchronization, safely handling concurrent DMA engine access.

---

## 2. DART Specification Mapping

| Capability | Apple DART (T8120 Hardware) | WinIOS `VirtualIommu` Implementation |
|---|---|---|
| Translation Granule | 16KB default (configurable 4KB) | 16KB granule default, arbitrary page sizes supported |
| Page Table Format | Multi-level radix tree | Hierarchical `std::unordered_map` with fast cache |
| Isolation Domains | Multiple hardware stream contexts | Dynamic `IommuDomainId` contexts (0 .. 64) |
| Device IDs | Stream IDs per SoC peripheral endpoint | 32-bit `DeviceId` binding (`DART_DEV_DMA`, `DART_DEV_NVME`) |
| Fault Behavior | Interrupt signal + transaction abort | `IommuFaultStatus` + error code returned to DMA master |
| Serialization | Hardware state restored across suspend | Full binary serialization (`serialize()`, `deserialize()`) |

---

## 3. Verification & Test Evidence

Validated through `stage199_4_iommu_architecture_tests`:
1. `test_domain_creation_and_lifecycle`: Verified dynamic allocation, retrieval, and teardown of domains.
2. `test_iova_to_pa_translation`: Verified mapping of IOVA ranges to physical RAM, offset preservation, and permission flags.
3. `test_permission_enforcement`: Read-only mappings correctly reject write attempts with `IommuFaultStatus::PermissionFault`.
4. `test_unmapped_access_fault`: Access to unmapped IOVA yields `IommuFaultStatus::TranslationFault`.
5. `test_tlb_caching_and_invalidation`: Verified translation acceleration via TLB and eviction upon unmap/invalidation.
