# WinIOS Campaign 198 Final Engineering Report

**Campaign:** 198 — ARM64 Memory-System Integration, Cache Coherency, DMA Consistency & SMP Virtual Platform Conformance  
**Target Architecture:** ARMv8.1-A / ARM64e  
**Target Research Device:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Granules:** 16 KB Page Translation Granule | 64-Byte Cache Line / Exclusive Reservation Granule  
**Host Environment:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0  
**Baseline Commit:** `0791c76822c864c802da65893fc9671b3879b857`  
**Branch:** `campaign-191r`  

---

## 1. Executive Summary

Campaign 198 successfully executed all 20 planned engineering stages (198.0 through 198.19), establishing a fully integrated, coherent, and robust ARM64 memory system for the WinIOS virtual platform.

Key achievements of Campaign 198:
1. **Unified Guest Memory Subsystem (`UnifiedMemoryView`):** Centralized all virtual memory accesses, eliminating translation bypass defects (notably in `DC ZVA`), enforcing alignment rules according to `SCTLR_EL1.A`, page permissions (`AP[2:1]`, `PXN`, `UXN`), and 16 KB boundary crossing byte-stitching.
2. **ARMv8 Memory Attribute & Device Restrictions:** Decoded `MAIR_EL1` and translation table descriptors into `MemoryType::Normal` vs `MemoryType::Device` (`Device_nGnRnE`, `Device_nGnRE`, `Device_nGRE`, `Device_GRE`). Strictly enforced that unaligned accesses, atomic RMW instructions, and `DC ZVA` fault when issued to Device memory.
3. **Cross-Core Translation & Broadcast TLBI:** Enforced cluster-wide TLBI broadcasts (`TLBI ALLE1IS`, `TLBI VAE1IS`, `TLBI ASIDE1IS`, `TLBI VAAE1IS`) and verified ASID isolation across processes and safe page re-use.
4. **Cache Architecture & Maintenance Operations:** Standardized all cache operations to 64 bytes matching Apple A16 Bionic hardware. Implemented and verified `DC ZVA`, `DC CVAC`, `DC CVAU`, `DC CIVAC`, `DC IVAC`, and instruction cache operations `IC IVAU`, `IC IALLU`, and `IC IALLUIS` with EL0 access trapping (`SCTLR_EL1.DZE`, `SCTLR_EL1.UCI`).
5. **Instruction Cache Synchronization & Self-Modifying Code:** Verified the full architectural sequence `STR` -> `DC CVAU` -> `DSB ISH` -> `IC IVAU` -> `DSB ISH` -> `ISB` through guest code execution.
6. **Synthetic DMA Engine Integration & Coherency:** Bound `VirtualDmaEngine` directly to `SmpSharedMemory` and `SmpExclusiveMonitorManager`. Verified that DMA writes snoop and invalidate multicore exclusive reservations on affected 64-byte granules.
7. **8-Core SMP Virtual Platform Conformance:** Scaled `SMPCluster` to 8 cores with shared physical RAM, per-core MMU instances, 8-way exclusive reservations, and PSCI v1.0+ secondary core bring-up.
8. **96 LSE Atomic Instruction Matrix:** Verified all 96 ARMv8.1-A LSE atomic operations (CAS, SWP, LDADD, LDCLR, LDEOR, LDSET across 4 widths and 4 orderings) under unified memory and exclusive monitor integration.
9. **Advanced SMP Litmus Tests:** Validated Message Passing (MP with DMA), Store Buffering (SB), Load Buffering (LB), Write-to-Read Causality (WRC), and cache maintenance synchronization against ARMv8 other-multicopy atomicity.
10. **System Snapshot & Deterministic Replay:** Integrated full `.wsnap` v2 snapshotting (persisting CPU state, MMU sysregs, physical memory, and DMA state) and verified 100% bit-exact replay determinism across repeated executions.
11. **Freestanding Guest Workload Execution:** Authored and cross-compiled `guest_memory_dma_cache_workload` with Zig 0.17.0, executing self-modifying code, cache maintenance, and LSE atomics in the guest.
12. **Differential Architectural Validation:** Validated DC ZVA, LSE atomics, DMA, and divergence diagnostics against the independent reference model.
13. **Comprehensive Regression Testing:** Ran full CTest regression suites in both Release and Debug configurations across all 432 project test suites with 100% pass of runnable tests (431 passed, 1 skipped). Zero compiler warnings under `/W4 /WX /permissive-`.

---

## 2. Evidence Classification Summary

- `GUEST_MEMORY_MODEL`: Stages 198.1, 198.2, 198.3, 198.7, 198.12, 198.13, 198.14, 198.15, 198.17, 198.19
- `GUEST_CACHE`: Stages 198.4, 198.5, 198.6
- `GUEST_DMA`: Stages 198.8, 198.9, 198.10, 198.16
- `SYNTHETIC_KERNEL`: Stage 198.11
- `REFERENCE_COMPARISON`: Stage 198.18

All claims strictly adhere to research reality: synthetic kernel contracts, freestanding Darwin Mach-O userspace execution, and rigorous architectural emulation without claiming proprietary Apple kernel or iOS boot capability.

---

## 3. Test Results Overview

| Configuration | Total Tests | Passed | Skipped | Failed | Duration |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Release** | 432 | 431 | 1 (`inferno_integration_tests`) | 0 | 85.45 s |
| **Debug** | 432 | 431 | 1 (`inferno_integration_tests`) | 0 | ~220 s |

---

## 4. Conclusion & Next Steps

Campaign 198 firmly closes the gap between CPU execution, virtual memory translation, cache operations, DMA controllers, and multicore synchronization. The virtual platform is now prepared for advanced synthetic kernel services, IPC scaling, and rich Darwin userspace execution in subsequent campaigns.
