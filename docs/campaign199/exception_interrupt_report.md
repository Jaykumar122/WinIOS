# WinIOS Campaign 199: Exception Levels & Virtual GIC Architecture Report

**Subsystems:** ARM64 Exception Levels (EL0/EL1/EL2) & Virtual GICv3 Interrupt Controller  
**Target Profile:** ARMv8.5-A / Apple A16 Bionic `T8120`  
**Classification:** `EXCEPTION_ARCHITECTURE` / `VIRTUAL_GIC`  
**Status:** FULLY IMPLEMENTED & VERIFIED  

---

## 1. Exception Level Integration (EL0, EL1, EL2)

In Campaign 199 Stage 12, WinIOS extended its architectural privilege framework to support nested privilege transitions:
1. **Privilege Transitions:**
   - User space execution at `EL0`.
   - Kernel space execution at `EL1`.
   - Virtual machine monitor / hypervisor control at `EL2`.
   - System calls (`SVC`) from EL0 target EL1 vectors with `EC = 0x15`.
   - Hypervisor calls (`HVC`) from EL1 target EL2 vectors with `EC = 0x16`.
2. **Context Preservation & Restoration:**
   - Exception entry saves target PC to `ELR_ELx`, processor state to `SPSR_ELx`, and generates architectural syndromes in `ESR_ELx`.
   - Exception return (`ERET`) restores PC from `ELR_ELx` and PSTATE from `SPSR_ELx`.
   - Detection and fault generation for illegal execution states (e.g., attempt to return to unsupported EL3 or corrupt PSTATE).

---

## 2. Virtual GICv3 Interrupt Architecture

In Stage 13 and 14, the GICv3 interrupt controller emulation was expanded to model multi-core virtualization:
1. **Distributor (`GICD`) & Redistributors (`GICR`):**
   - Central Distributor at `0x08000000` managing Shared Peripheral Interrupts (SPIs, INTIDs 32..1019).
   - Per-core Redistributor instances at `0x080A0000 + core * 0x20000` managing Private Peripheral Interrupts (PPIs, INTIDs 16..31) and Software Generated Interrupts (SGIs, INTIDs 0..15).
2. **Interrupt Grouping & Routing:**
   - Support for Group 0 (FIQ) and Group 1 (IRQ) interrupts.
   - Core Priority Mask Register (`ICC_PMR_EL1`) filtering lower-priority interrupts.
3. **Hypervisor Virtual Interrupt Injection:**
   - Implemented Virtual CPU Interface List Registers (`ICH_LR<n>_EL2`).
   - Hypervisor at EL2 programs virtual interrupts (Virtual INTID, physical INTID mapping, priority, state) into List Registers for zero-overhead hardware virtualization injection to guest EL1/EL0.
4. **Inter-Processor Signaling (SGI):**
   - Software Generated Interrupts dispatched across cores via `ICC_SGI1R_EL1` write simulation.
5. **Multi-Core Affinity Routing (`GICD_IROUTER`):**
   - SPI affinity registers configured per SPI starting at `0x6100` (`GICD_IROUTER32`..).
   - Interrupt lines correctly target specific cores (e.g. Core 3) based on affinity configuration while maintaining quiescence on other cores.

---

## 3. Verification Evidence

- `stage199_12_exception_level_tests`:
  - Verified EL0 -> EL1 via SVC and return via ERET.
  - Verified EL1 -> EL2 transition via exception delivery.
  - Verified illegal state detection when ERET targets invalid configuration.
- `stage199_13_virtual_gic_tests`:
  - Verified Distributor/Redistributor multi-core register layout.
  - Verified Group 0 (FIQ) vs Group 1 (IRQ) signal differentiation.
  - Verified priority masking via `ICC_PMR_EL1`.
  - Verified virtual interrupt injection via `ICH_LR_EL2`.
  - Verified SGI inter-core dispatch.
- `stage199_14_multicore_interrupt_tests`:
  - Verified device-generated interrupt injection via `VirtualDmaEngine`.
  - Verified 8-core affinity routing under `GICD_IROUTER`.
