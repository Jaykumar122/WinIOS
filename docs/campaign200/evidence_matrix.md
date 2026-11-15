# WinIOS Campaign 200: Evidence Verification Matrix

**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Platform:** Windows 11 x64 (MSVC C++20, `/W4 /WX /permissive-`)  
**Campaign Scope:** Stages 200.0 through 200.24  
**Date:** November 2026  

---

## Evidence Taxonomy & Classification Keys

- `STAGE2_TRANSLATION`: Stage-2 MMU translation, IPA-to-PA mapping, VTCR_EL2 configuration, combined S1+S2 translation, fault hierarchy.
- `VIRTUAL_IOMMU`: Virtual IOMMU domains, device attachment, IOVA-to-PA translation, fault generation, DMA engine isolation.
- `ARM64E_PAC_BOUNDED`: ARMv8.3-A / ARM64e pointer authentication instructions, QARMA hashing, key isolation, privilege boundaries, trap exceptions.
- `VIRTUAL_GIC`: GICv3 distributor/redistributor, SGIs/PPIs/SPIs, PMR priority masking, multi-core affinity routing across 8 cores.
- `DEVICE_TREE`: Apple-format flattened device tree (FDT/DTB), A16 Bionic chip-id `0x8120`, 8 CPU nodes, memory, peripheral bindings.
- `GUEST_WORKLOAD`: Genuine ARM64e Mach-O binary compiled with Zig 0.17.0 for Apple A16 Bionic, executed directly in guest memory.
- `SNAPSHOT_REPLAY`: Multi-core state serialization, memory hashing, snapshot restore, bit-exact deterministic replay.

---

## Exhaustive Stage Verification Matrix

| Stage | Subsystem / Objective | Evidence Key | Verification Source / Test Target | Status |
| :--- | :--- | :--- | :--- | :--- |
| **200.0** | Baseline repository audit, commit verification, and build check | `BASELINE_AUDIT` | `docs/campaign200/stage200_0_baseline_audit.md` | **PASSED** |
| **200.1** | Architecture dependency and interface contract analysis | `ARCH_AUDIT` | `docs/campaign200/architecture_dependency_audit.md` | **PASSED** |
| **200.2** | Cross-subsystem integration contract definitions | `INTEG_CONTRACTS` | `docs/campaign200/integration_contracts.md` | **PASSED** |
| **200.3** | Stage-1 MMU 16KB table walk, AP[2:1] permissions, UXN/PXN, AF faults | `STAGE1_TRANSLATION` | `stage200_3_stage1_translation_tests.exe` | **PASSED** |
| **200.4** | Stage-2 MMU VTCR_EL2, 16KB/4KB granule, S2AP permissions, XN | `STAGE2_TRANSLATION` | `stage200_4_stage2_translation_tests.exe` | **PASSED** |
| **200.5** | Combined S1+S2 translation, strictest permission intersection, fault priority | `STAGE2_TRANSLATION` | `stage200_5_combined_fault_propagation_tests.exe` | **PASSED** |
| **200.6** | ASID/VMID TLB caching, targeted invalidations, multi-core broadcast | `STAGE2_TRANSLATION` | `stage200_6_tlb_concurrency_tests.exe` | **PASSED** |
| **200.7** | MAIR_EL1 memory decoding, DC ZVA 64-byte clearing, exclusive monitor | `CACHE_COHERENCY` | `stage200_7_memory_cache_integration_tests.exe` | **PASSED** |
| **200.8** | Virtual IOMMU lifecycle, domain creation, device attach/detach, sharing | `VIRTUAL_IOMMU` | `stage200_8_iommu_lifecycle_tests.exe` | **PASSED** |
| **200.9** | IOVA-to-PA translation, unmapped IOVA fault, write-to-RO fault, DMA halt | `VIRTUAL_IOMMU` | `stage200_9_iova_fault_tests.exe` | **PASSED** |
| **200.10** | Domain DMA isolation, CPU-DMA coherency, exclusive monitor invalidation | `VIRTUAL_IOMMU` | `stage200_10_dma_coherency_tests.exe` | **PASSED** |
| **200.11** | DMA descriptor completion interrupt (GICv3 SPI 48), ack/EOI lifecycle | `VIRTUAL_IOMMU` | `stage200_11_dma_interrupt_tests.exe` | **PASSED** |
| **200.12** | PACIA, PACIB, PACDA, PACDB, PACIZA, PACIZB, AUT*, XPAC*, PACGA, BRAA | `ARM64E_PAC_BOUNDED`| `stage200_12_pac_conformance_tests.exe` | **PASSED** |
| **200.13** | PAC key register isolation (`min_el == 1`), EL0 traps, context switch isolation | `ARM64E_PAC_BOUNDED`| `stage200_13_pac_isolation_tests.exe` | **PASSED** |
| **200.14** | PAC exception handling, TrapException (EC 0x1C), CorruptUpperBits, RETAA | `ARM64E_PAC_BOUNDED`| `stage200_14_pac_exception_tests.exe` | **PASSED** |
| **200.15** | Privilege level transitions (EL0 $\leftrightarrow$ EL1), SVC #imm, ERET, illegal ERET | `PRIVILEGE_LEVEL` | `stage200_15_exception_level_tests.exe` | **PASSED** |
| **200.16** | GICv3 Distributor/Redistributor decoding, Group 0/1, PMR masking, IAR/EOI | `VIRTUAL_GIC` | `stage200_16_gicv3_conformance_tests.exe` | **PASSED** |
| **200.17** | GICv3 8-core affinity routing, concurrent multi-threaded interrupt stress | `VIRTUAL_GIC` | `stage200_17_multicore_interrupt_race_tests.exe` | **PASSED** |
| **200.18** | Apple DTB model "iPhone15,4", chip-id 0x8120, 8 CPU nodes, FDT magic | `DEVICE_TREE` | `stage200_18_devicetree_consistency_tests.exe` | **PASSED** |
| **200.19** | Synthetic boot contract handoff ($X0..X3$, EL1h, DAIF, SP_EL1) | `BOOT_HANDOFF` | `stage200_19_boot_handoff_tests.exe` | **PASSED** |
| **200.20** | Multi-core complete snapshot serialization (CPU, MMU, IOMMU, GICv3, RAM) | `SNAPSHOT_REPLAY` | `stage200_20_complete_snapshot_tests.exe` | **PASSED** |
| **200.21** | Snapshot restoration, state recovery, corrupt blob rejection | `SNAPSHOT_REPLAY` | `stage200_21_snapshot_recovery_tests.exe` | **PASSED** |
| **200.22** | Checkpoint restoration and 100% bit-exact deterministic replay | `SNAPSHOT_REPLAY` | `stage200_22_deterministic_replay_tests.exe` | **PASSED** |
| **200.23** | Execution of genuine ARM64e Mach-O binary compiled with Zig 0.17.0 | `GUEST_WORKLOAD` | `stage200_23_guest_workload_tests.exe` | **PASSED** |
| **200.24** | Full test suite verification in Debug and Release, closeout documentation | `CLOSEOUT` | `docs/campaign200/campaign200_final_report.md` | **PASSED** |

---

## Comprehensive Summary

- **Total Test Suites Executed in Campaign 200:** 21 unit test suites (`stage200_3` through `stage200_23`).
- **Pass Rate:** 100% (21/21 passed).
- **Compiler Warnings:** Zero warnings under MSVC C++20 `/W4 /WX /permissive-`.
- **Integrity Validation:** Every stage verified through dedicated deterministic test fixtures.
