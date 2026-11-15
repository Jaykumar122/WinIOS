# WinIOS Campaign 199 — Stage 199.0 Baseline Preservation & Audit Report

**Campaign:** 199 — Advanced ARM64e Virtual Platform, Stage-2 Translation, IOMMU, DMA Isolation, Pointer Authentication & Device Architecture  
**Date:** 2026-11-15  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target Architecture:** ARMv8.1-A / ARM64e research configuration  
**Target Research Device:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Environment:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0  
**Baseline Commit:** `da4ddbf` (`da4ddbf Campaign 198: ARM64 Memory-System Integration, Cache Coherency, DMA Consistency & SMP Virtual Platform Conformance`)  
**Branch:** `campaign-191r`  

---

## 1. Repository State & Integrity Audit

- **Git Branch:** Verified `campaign-191r`.
- **Git HEAD Commit:** Verified `da4ddbf` (Campaign 198 closeout).
- **Working Tree:** Verified clean (all prior campaign modifications preserved, zero untracked debris).
- **Protected Files Integrity:** Verified pristine and unmodified:
  - `WinIOS_README.md`
  - `docs/progress.md`
  - `WinISO26_README.md`
  - `Inferno-Research/`

---

## 2. Review of Campaign 198 Deliverables & Baseline Test Suite

Campaign 198 established:
1. Unified memory access (`winios::memory::UnifiedMemoryView`) eliminating bypass defects in `DC ZVA`.
2. Memory attribute decoding (`decode_memory_attributes`) enforcing Device memory restrictions (no unaligned accesses, no atomic RMW, no `DC ZVA`).
3. Hardware-accurate 64-byte cache line and exclusive reservation granules matching Apple A16 Bionic.
4. Multicore DMA coherency with automatic exclusive monitor invalidation on DMA writes.
5. 8-core SMP cluster scaling with PSCI secondary boot and cross-core TLBI broadcasts.
6. 96-variant ARMv8.1-A LSE atomic instruction matrix.
7. Advanced litmus tests, .wsnap v2 snapshots, and deterministic replay.
8. 431 passed tests, 1 skipped (`inferno_integration_tests` requiring external assets), 0 failed across both Release and Debug configurations.

---

## 3. Subsystem Audit & Integration Dependencies for Campaign 199

### 3.1 Stage-1 & Stage-2 MMU Subsystems
- **Current State:**
  - `src/cpu/mmu.h` and `src/cpu/mmu.cpp` contain an existing Stage-1 MMU with basic EL2 register accessors (`HCR_EL2`, `VTTBR_EL2`, `VTCR_EL2`) and an initial 4KB-only `walk_stage2` helper.
  - Granule support is currently oriented towards 16KB Stage-1 (`TG0 = 0b10`), but lacks explicit 16KB Stage-2 page-table structures, IPA-to-PA translation classes, combined fault syndromes (`FSC` encoding with `is_stage2`), and VMID-isolated Stage-2 TLBs.
- **Campaign 199 Plan (Phase A):**
  - Implement a dedicated `winios::mmu::Stage2TranslationEngine` in `src/memory/` or `src/cpu/` with 16KB granule primary support (`VTCR_EL2.TG0 = 0b10`) and 4KB fallback.
  - Integrate combined two-stage translation (VA -> IPA -> PA) in `UnifiedMemoryView` and `mmu::MMU`.
  - Provide VMID-aware TLB caching and cross-core invalidation (`TLBI VMALLE1IS`, `TLBI IPAS2E1IS`).

### 3.2 Virtual IOMMU & DMA Security
- **Current State:**
  - `src/device/virtual_dma_engine.h` executes memory-to-memory copies and fills using raw guest physical addresses.
  - There is currently no device identity or domain isolation; any DMA descriptor can address any physical address in `SmpSharedMemory`.
- **Campaign 199 Plan (Phase B):**
  - Design and implement `VirtualIommu` in `src/device/` with Device IDs, Domain IDs, context tables, and IOMMU translation pages.
  - Extend `VirtualDmaEngine` with translated DMA mode alongside a legacy physical compatibility mode.
  - Implement cross-domain isolation barriers to prevent rogue devices from accessing other device domains.

### 3.3 ARM64e Pointer Authentication (PAC)
- **Current State:**
  - `src/cpu/pointer_auth.h` and `pointer_auth.cpp` define `Arm64ePointerAuth` with 128-bit keys (`APIA`, `APIB`, `APDA`, `APDB`, `APGA`) and FPAC synchronous exception traps (EC `0x1C`).
  - Cryptographic operations currently use a bounded Murmur3/64-bit mixer stub.
- **Campaign 199 Plan (Phase C):**
  - Audit architectural vs. cryptographic fidelity (`stage199_8_pac_architecture_audit.md`).
  - Implement system register accessors for key registers (`APIAKeyLo_EL1`, `APIAKeyHi_EL1`, etc.) with EL1-only privilege protection.
  - Support PAC instructions (`PACIA`, `PACIB`, `AUTIA`, `AUTIB`, `PACDA`, `PACDB`, `AUTDA`, `AUTDB`, `XPAC`, `PACGA`) and verify deterministic exception generation on corrupted pointers.

### 3.4 Exception Levels & Virtual Interrupt Controller
- **Current State:**
  - `src/cpu/gicv3.h` implements a full GICv3 distributor (`GICD`) and redistributor (`GICR`) with support for up to 8 cores, PMR, BPR, and PPI/SPI level lines.
  - `src/device/apple_aic.h` models Apple Interrupt Controller (AIC) registers.
- **Campaign 199 Plan (Phase D):**
  - Advance exception-level transitions between EL0, EL1, and EL2 virtual handling.
  - Validate multicore interrupt delivery, priority masking, preemption, and end-of-interrupt (`EOI`) handling without interrupt loss or spurious drops.

### 3.5 Device Tree & Platform Handoff
- **Current State:**
  - `src/device/apple_devicetree.h` provides an Apple ADTS builder/parser with an iPhone 15 profile generator.
  - `src/device/fdt_builder.h` builds standard Flattened Device Tree (DTB) blobs.
- **Campaign 199 Plan (Phase E):**
  - Extend the Device Tree to represent IOMMU nodes, DMA domains, GICv3/AIC interrupt lines, and platform memory maps.
  - Validate boot argument and platform handoff structures to freestanding guest workloads.

### 3.6 Snapshots, Security & Workload Validation
- **Current State:**
  - `src/snapshot/snapshot_serializer.h` serializes CPU registers, GIC, memory, and DMA state.
- **Campaign 199 Plan (Phase F):**
  - Extend `.wsnap` v2 to serialize Stage-2 tables, VMIDs, IOMMU domain tables, and PAC key registers.
  - Cross-compile freestanding guest workloads with Zig 0.17.0 and validate all 20 stages under both Release and Debug test regressions.

---

## 4. Stage 199.0 Outcome
- **Status:** PASS
- Baseline preserved, audit complete, dependencies identified. Ready to proceed with Stage 199.1.
