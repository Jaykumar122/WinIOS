# WinIOS Campaign 199: Device Tree & Platform Handoff Validation Report

**Subsystem:** Apple DeviceTree & Flattened Device Tree (FDT) Synthesis and Boot Handoff  
**Target Profile:** iPhone 15 / `iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`  
**Classification:** `DEVICE_TREE` / `PLATFORM_BOOT`  
**Status:** FULLY IMPLEMENTED & VERIFIED  

---

## 1. Architectural Scope

To support both synthetic Darwin/XNU kernel execution and Linux virtual machine boot, WinIOS provides dual device tree generators:
1. **Canonical Apple DeviceTree (`src/device/apple_devicetree.h/cpp`):**
   - Binary tree format matching genuine Apple iBoot handoff conventions.
   - Models the iPhone 15 SoC topology with:
     - Canonical Apple A16 Bionic cores: 2 high-performance Everest cores and 4 high-efficiency Sawtooth cores.
     - Required SoC nodes: `/arm-io/pmgr` (Power Manager), `/arm-io/disp0` (Display Pipeline), `/arm-io/ans2` (Apple NVMe Storage 2).
     - New Virtual Platform Extensions: `/arm-io/dart-dma` (`apple,t8120-dart`) and `/arm-io/virtual-dma` (`apple,virtual-dma`).
     - Hypervisor integration node: `/hypervisor` (`apple,hypervisor-stage2`).
2. **Flattened Device Tree (FDT / DTB) Generator (`src/device/fdt_builder.cpp`):**
   - Standard OpenFirmware DTB format.
   - Includes `/smmu` (ARM SMMUv3 / Apple DART equivalent) and `/hypervisor` nodes with translation granule attributes (`granule-size = <16384>`).
3. **Platform Boot & Handoff Contract (`SyntheticKernelBootInfo`):**
   - Verification that memory map adheres to 16KB page alignment.
   - Verification that boot arguments and DTB physical pointers are staged in memory.
   - Verification that CPU registers conform to Darwin/ARM64 boot specifications:
     - `X0`: Physical address of DeviceTree / boot arguments.
     - `CurrentEL = EL1` with SP_EL1 selected.
     - MMU and caches disabled or appropriately initialized.

---

## 2. Verification Evidence

- `stage199_15_devicetree_tests`:
  - `test_apple_devicetree_iphone15_platform`: Verified synthesis of complete Apple DeviceTree containing DART, DMA, Hypervisor, PMGR, DISP0, and ANS2 nodes, passing full validation.
  - `test_fdt_builder_smmu_hypervisor_nodes`: Verified FDT DTB emission and parsing containing SMMUv3 and Hypervisor nodes.
- `stage199_16_platform_boot_handoff_tests`:
  - `test_platform_boot_contract_verification`: Verified `SyntheticKernelBootInfo` validation checks 16KB page alignment, memory sizing, and DTB flags.
  - `test_darwin_boot_handoff_staging`: Verified staging of DTB in memory, CPU register configuration at EL1, and architectural handoff integrity.
