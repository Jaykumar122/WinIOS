# Known Limitations & Stage 36 Architectural Recommendations
**WinIOS Phase 52 Stage 35 Boundary Scope**

---

## 1. Active Boundaries & Explicitly Unsupported Features

1. **Translation Granules:**
   - **4KB Granule:** Fully supported and verified across Levels 0, 1, 2, and 3.
   - **16KB and 64KB Granules:** Explicitly unsupported in the current translation engine. `TCR_EL1.TG0` values corresponding to 16KB/64KB are rejected with configuration warnings.
2. **Topology Ceiling:**
   - SMP scaling supports 1, 2, 4, and 8 virtual cores. Topologies exceeding 8 virtual cores are strictly rejected by capability gates (`BLK-SMP-04`).
3. **Hardware DMA Passthrough:**
   - Only synthetic, deterministic, synchronous `VirtualDmaDevice` operations are supported. Direct host PCI/PCIe passthrough is disallowed to maintain replay safety (`BLK-SMP-03`).
4. **Armv8 Architectural Breadth:**
   - Complete Armv8-A architecture (e.g., SVE, Pointer Authentication, Nested Virtualization) is not modeled.
   - Complete iOS boot capability is not claimed; implementation focuses on deterministic architectural state integrity, memory management, and deterministic replay.

---

## 2. Recommendations for Stage 36

1. **Stage 2 MMU Expansion:** Integrate Stage 2 IPA translation into the independent reference model (`ReferencePageTableWalker`) to differentially verify hypervisor (EL2) paging structures.
2. **Virtual Timer Cross-Clock Drift Auditing:** Formalize independent differential verification of `CNTVCT_EL0` against `VirtualTime` reference counters under dynamic frequency scaling scenarios.
3. **Extended Syndromes for Asynchronous SError:** Extend `ReferenceSyndromeModel` to support SError Syndrome structures (ISS field for asynchronous memory errors).
