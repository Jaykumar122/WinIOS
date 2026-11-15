# WinIOS Campaign 199: Evidence Matrix

**Target Device Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Campaign:** 199.0 – 199.19  
**Execution Environment:** Windows 11 x64, MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0  

---

| Stage | Subsystem / Objective | Key Deliverables | Test Target / Evidence | Classification | Status |
|---|---|---|---|---|---|
| **199.0** | Baseline Audit | Tree audit, clean state, ignore rules | `docs/campaign199/stage199_0_baseline_audit.md` | `BASELINE_AUDIT` | PASSED |
| **199.1** | Stage-2 MMU Subsystem | `Stage2TranslationEngine`, 16KB/4KB/64KB granules | `stage199_1_stage2_translation_tests` (4/4) | `STAGE2_TRANSLATION` | PASSED |
| **199.2** | Combined S1/S2 Translation | `MMU::walk_stage2`, VA->IPA->PA walk, permission AND | `stage199_2_combined_translation_tests` (3/3) | `STAGE2_TRANSLATION` | PASSED |
| **199.3** | Stage-2 TLB & Invalidation | VMID caching, `TLBI IPAS2E1IS`, `TLBI VMALLS12E1IS` | `stage199_3_stage2_tlb_invalidation_tests` (3/3) | `STAGE2_TRANSLATION` | PASSED |
| **199.4** | Virtual IOMMU Architecture | `VirtualIommu`, `IommuDomain`, Apple DART model | `stage199_4_iommu_architecture_tests` (4/4) | `VIRTUAL_IOMMU` | PASSED |
| **199.5** | DMA IOVA Translation | `VirtualDmaEngine` IOVA translation, fault handling | `stage199_5_dma_translation_tests` (3/3) | `DMA_ISOLATION` | PASSED |
| **199.6** | Device Isolation & Domains | Cross-domain protection, unattached device rejection | `stage199_6_device_isolation_tests` (3/3) | `DMA_ISOLATION` | PASSED |
| **199.7** | IOMMU & SMP Coherency | DMA write exclusive monitor invalidation across cores | `stage199_7_iommu_smp_integration_tests` (2/2) | `SMP_COHERENCY` | PASSED |
| **199.8** | PAC Architecture Audit | Architectural review of A16 Bionic PAC design | `docs/campaign199/stage199_8_pac_architecture_audit.md` | `ARM64E_PAC_BOUNDED` | PASSED |
| **199.9** | PAC Keys & System Registers | 10 PAC sysregs, EL0 privilege trap, 128-bit keys | `stage199_9_pac_key_register_tests` (5/5) | `ARM64E_PAC_BOUNDED` | PASSED |
| **199.10** | PAC Instruction Semantics | 16 PAC 1-src ops, PACGA, BRAA, BLRAA, RETAA | `stage199_10_pac_instruction_tests` (3/3) | `ARM64E_PAC_BOUNDED` | PASSED |
| **199.11** | PAC Faults & Exceptions | `EC = 0x1C` synchronous PAC trap delivery | `stage199_11_pac_fault_tests` (5/5) | `ARM64E_PAC_BOUNDED` | PASSED |
| **199.12** | Exception Level Integration | EL0/EL1/EL2 transitions via SVC/ERET, illegal state | `stage199_12_exception_level_tests` (5/5) | `EXCEPTION_ARCHITECTURE`| PASSED |
| **199.13** | Virtual GICv3 Architecture | GICD/GICR, Group 0/1, PMR, `ICH_LR_EL2`, SGI dispatch| `stage199_13_virtual_gic_tests` (4/4) | `VIRTUAL_GIC` | PASSED |
| **199.14** | Multicore Interrupt Routing | DMA interrupt to SPI 48/50, 8-core `GICD_IROUTER` | `stage199_14_multicore_interrupt_tests` (3/3) | `VIRTUAL_GIC` | PASSED |
| **199.15** | Device Tree Architecture | Apple DeviceTree & FDT with DART, DMA, Hypervisor | `stage199_15_devicetree_tests` (2/2) | `DEVICE_TREE` | PASSED |
| **199.16** | Platform Boot & Handoff | `SyntheticKernelBootInfo` contract, 16KB alignment | `stage199_16_platform_boot_handoff_tests` (2/2) | `PLATFORM_BOOT` | PASSED |
| **199.17** | Snapshot & Replay | Binary serialization for Stage2, IOMMU, PAC | `stage199_17_snapshot_iommu_pac_tests` (4/4) | `SNAPSHOT_REPLAY` | PASSED |
| **199.18** | Genuine Guest Workload | Mach-O binary compiled with Zig (`-mcpu=apple_a16`) | `stage199_18_guest_workload_tests` (4/4) | `GUEST_WORKLOAD` | PASSED |
| **199.19** | Integration & Closeout | Full CTest regression, audit manifest, closeout | `docs/campaign199/test_results_release.txt` | `INTEGRATION_REGRESSION`| PASSED |
