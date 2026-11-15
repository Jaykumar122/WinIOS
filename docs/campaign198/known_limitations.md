# WinIOS Campaign 198: Known Limitations & Research Boundaries

**Target Architecture:** ARMv8.1-A / ARM64e (`iPhone15,4` / Apple A16 Bionic `T8120`)  
**Scope:** Memory-System Integration, Cache Coherency, DMA Consistency, and SMP Virtual Platform  
**Classification Policy:** Strict truthfulness; no false claims of genuine Apple kernel execution.

---

## 1. Emulation vs. Silicon Realities

1. **Virtual vs. Physical Cache Simulation:**
   - In WinIOS, physical cache hierarchies (L1 Data, L1 Instruction, L2 Unified, System Level Cache / SLC) are simulated via architectural behavioral contracts rather than timing-accurate cycle models.
   - Cache maintenance instructions (`DC CVAU`, `DC CIVAC`, `IC IVAU`, `DC ZVA`) enforce architectural invariants, permission checks, and synchronization barriers, but do not simulate hardware cache eviction delays or line replacement policies (LRU/pseudo-LRU).

2. **DMA Interconnect Modeling:**
   - The DMA controller is a synthetic virtual engine (`VirtualDmaEngine`) designed to validate memory coherency, reservation invalidation, and snapshot/replay determinism. It models memory-mapped DMA queues and descriptor chaining, but does not simulate PCIe root complex, IOMMU/DART (Device Address Resolution Table), or Apple Neural Engine DMA channels.

3. **Weak-Memory Interleaving Modeling:**
   - On the Windows x64 host (which exhibits strong TSO memory ordering), ARMv8 weak-memory reorderings (such as relaxed store buffering) are simulated via `GuestMemoryOrderModel` and stochastic multi-core scheduling rather than hardware out-of-order execution pipelines.

---

## 2. Kernel & Software Stack Scope

1. **Synthetic Kernel Contract:**
   - The kernel execution environment in WinIOS remains a synthetic research kernel implementing Darwin ABI, Mach traps, BSD syscalls, and synthetic page tables.
   - Genuine Apple XNU kernelcache boot requires proprietary hardware components (Apple Secure Enclave Processor / SEP, proprietary Apple PMGR power domains, SPRR/GXF permission registers, and proprietary NVRAM/APFS boot manifests) that are outside the scope of userspace Darwin emulation.

2. **Mach-O Artifact Execution:**
   - Guest binary execution is certified for freestanding Mach-O executables cross-compiled with Zig 0.17.0 and open-source libraries (cJSON, SHA-256, uzlib). It does not claim execution of proprietary Apple system daemons (`launchd`, `SpringBoard`) requiring signed entitlements and FairPlay DRM.

---

## 3. Test Coverage Boundaries

- **Single Skipped Test:** `383 - inferno_integration_tests` is intentionally skipped when running outside the standalone Inferno Research subproject harness.
- All other 431 test suites run and pass in both Release and Debug configurations.
