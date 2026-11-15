# WinIOS Campaign 199: Architecture Audit

**Target Research Device Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Scope:** Complete Implementation Audit across Stages 199.0 – 199.19  
**Host Environment:** Windows 11 x64, MSVC C++20 (`/W4 /WX /permissive-`)  
**Cross-Compiler:** Zig 0.17.0 (`aarch64-macos`, `-mcpu=apple_a16`)  

---

## 1. Subsystem Implementation Inventory

### 1.1 Memory Subsystem & Stage-2 MMU
- `src/memory/stage2_mmu.h/cpp`:
  - Implements `Stage2TranslationEngine` supporting 16KB default granule, 4KB and 64KB alternatives.
  - Multi-level table walking from Level 0 to Level 3.
  - Granular permission checking (`Stage2Permissions`: Read, Write, Execute).
  - VMID-tagged TLB cache with invalidation routines (`TLBI VMALLS12E1IS`, `TLBI IPAS2E1IS`).
  - Binary snapshot serialization and deserialization.
- `src/cpu/mmu.cpp`:
  - Added `MMU::walk_stage2` and `MMU::translate_combined`.
  - Performs seamless VA -> IPA -> PA translation when both Stage 1 and Stage 2 are active.
  - Intersects permissions across stages.

### 1.2 Virtual IOMMU & DMA Engine Isolation
- `src/device/virtual_iommu.h/cpp`:
  - Implements `VirtualIommu` modeled after Apple DART (`apple,t8120-dart`).
  - Manages discrete `IommuDomain` instances with individual IOVA->PA mapping tables.
  - Device attachment mapping `DeviceId` -> `IommuDomainId`.
  - Thread-safe access via `std::recursive_mutex`.
  - Binary state serialization and restoration.
- `src/device/virtual_dma_engine.cpp`:
  - Integrates `VirtualIommu` for descriptor-based IOVA translation.
  - Generates DMA completion interrupts to GICv3 SPI 48/50.
  - Dispatches exclusive monitor invalidations across SMP cluster upon memory modification.

### 1.3 ARM64e Pointer Authentication (PAC)
- `src/cpu/pointer_auth.h/cpp`:
  - Implements `Arm64ePointerAuth` reference engine.
  - 5 architectural 128-bit keys: `APIAKey`, `APIBKey`, `APDAKey`, `APDBKey`, `APGAKey`.
  - Deterministic signing, authentication, signature stripping, and generic MAC calculation.
  - Fault syndrome generation and architectural error bit tagging.
- `src/cpu/sysreg.h/cpp`:
  - Integrated 10 PAC system registers (`APIAKeyLo_EL1`, `APIAKeyHi_EL1`, etc.).
  - Enforced EL1/EL2 privilege checks; EL0 access raises Undefined Instruction exception.
- `src/cpu/cpu.cpp`:
  - Implemented 16 PAC 1-source instructions: `PACIA`, `PACIB`, `PACDA`, `PACDB`, `AUTIA`, `AUTIB`, `AUTDA`, `AUTDB`, `PACIZA`, `PACIZB`, `PACDZA`, `PACDZB`, `AUTIZA`, `AUTIZB`, `AUTDZA`, `AUTDZB`, `XPACI`, `XPACD`.
  - Implemented `PACGA` 2-source instruction.
  - Implemented authenticated branches: `BRAA`, `BRAB`, `BLRAA`, `BLRAB`, `RETAA`, `RETAB`.
  - Wired `EC = 0x1C` synchronous PAC trap delivery.

### 1.4 Exception Levels & Virtual GICv3
- `src/cpu/cpu.cpp`:
  - Multi-level exception handling across EL0, EL1, EL2.
  - Syndromes in `ESR_EL1`, `ESR_EL2`, `FAR_EL1`, `FAR_EL2`, `HPFAR_EL2`.
- `src/device/gicv3.cpp`:
  - Multi-core Distributor and per-core Redistributors.
  - Group 0 (FIQ) and Group 1 (IRQ) differentiation.
  - Priority filtering via `ICC_PMR_EL1`.
  - Virtual interrupt injection via List Registers (`ICH_LR_EL2`).
  - Affinity routing via `GICD_IROUTER` for 8-core virtual topology.

### 1.5 Device Tree & Boot Protocol
- `src/device/apple_devicetree.cpp`:
  - Added `create_iphone15_virtual_platform_devicetree` with DART, DMA, and Hypervisor nodes alongside canonical Everest/Sawtooth cores, PMGR, DISP0, and ANS2.
- `src/device/fdt_builder.cpp`:
  - Added SMMUv3 and Hypervisor node generation.
- `src/runtime/synthetic_kernel_boot.cpp`:
  - Verified 16KB page alignment and Darwin EL1 CPU register staging contract.

---

## 2. Code Quality & Standards Conformance

- **Language Standard:** C++20 throughout.
- **Compiler Compliance:** Compiled with Visual Studio 2026 MSVC under `/W4 /WX /permissive-` with zero warnings and zero errors.
- **Cross-Platform Reproducibility:** Freestanding Mach-O binaries cross-compiled using Zig 0.17.0 (`-mcpu=apple_a16`) producing deterministic machine code.
- **Safety & Boundary Guardrails:** Never attempts to claim genuine Apple firmware boot; all implementations are clearly categorized as `STAGE2_TRANSLATION`, `VIRTUAL_IOMMU`, `ARM64E_PAC_BOUNDED`, and `SYNTHETIC_KERNEL`.
