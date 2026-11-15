# WinIOS Campaign 200: Platform Boot Handoff Validation Report

**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`) Virtual Platform  
**Stage:** 200.19  
**Status:** VALIDATED  
**Evidence Classification:** `DEVICE_TREE` & `VIRTUAL_GIC`  

---

## 1. Executive Summary

This report documents the architectural conformance of the WinIOS platform handoff and interrupt subsystem under Campaign 200. The synthetic bootloader contract transitions execution from virtual firmware/loader into guest kernel code with deterministic architectural register states, complete DeviceTree topology, and initialized GICv3 interrupt infrastructure.

The validation covers:
1. **GICv3 Interrupt Controller Conformance:** Distributor (`GICD`) and Redistributor (`GICR`) register decoding, Group 0 (FIQ) and Group 1 (IRQ) classification, and `ICC_PMR_EL1` priority masking.
2. **Multicore Interrupt Routing:** Core affinity routing via `GICD_IROUTERn` across an 8-core virtual topology, validated under concurrent multi-threaded stress.
3. **Apple DeviceTree Integrity:** Conformance of the synthesized DeviceTree to the iPhone 15 hardware profile (`iPhone15,4`, Apple A16 Bionic `T8120`, board ID `D37AP`, 8 CPU cores, 16KB page granule property).
4. **Synthetic Boot Handoff Contract:** Architectural register guarantees ($X0 = \text{DTB PA}$, $X1 = X2 = X3 = 0$, $\text{CurrentEL} = 1$, $\text{SPSel} = 1$, $\text{DAIF} = 0\text{x}3\text{C}0$, $SP\_EL1$ initialized).

---

## 2. Test Verification Matrix

| Stage | Test Suite | Scope & Invariants Tested | Status |
|---|---|---|---|
| **200.16** | `stage200_16_gicv3_conformance_tests` | MMIO decoding, Group 0/1 classification, PMR masking, and IAR/EOI lifecycle. | PASS |
| **200.17** | `stage200_17_multicore_interrupt_race_tests` | SPI affinity routing across 8 cores, concurrent multi-threaded interrupt assertions and acknowledges. | PASS |
| **200.18** | `stage200_18_devicetree_consistency_tests` | Apple DTB model ("iPhone15,4"), chip-id (0x8120), 8 CPUs, memory node, and FDT header magic (0xD00DFEED). | PASS |
| **200.19** | `stage200_19_boot_handoff_tests` | Boot contract validation ($X0..X3$, EL1h, DAIF, SP_EL1) and first instruction execution step from kernel entry. | PASS |

---

## 3. Boot Handoff Architectural Guarantees

At the moment control is transferred to the synthetic kernel entry point, the primary core (Core 0) satisfies the following architectural preconditions:

$$\begin{aligned}
\text{CurrentEL} &= 1 \quad (\text{EL1h with SPSel} = 1) \\
X_0 &= \text{Physical Address of Flattened Device Tree} \\
X_1, X_2, X_3 &= 0 \quad (\text{Architecturally reserved}) \\
\text{DAIF} &= 0\text{x}3\text{C}0 \quad (\text{Debug, SError, IRQ, FIQ masked}) \\
SP\_EL1 &= \text{Top of allocated kernel boot stack} \\
PC &= \text{Kernel entry address}
\end{aligned}$$

Secondary cores (Cores 1..7) remain parked in a low-power quiescent state awaiting PSCI `CPU_ON` calls before entering the synthetic kernel execution loop.

---

## 4. Conclusion

Phase E establishes that the WinIOS virtual platform correctly implements the ARM64 boot architecture, GICv3 interrupt routing, and iPhone 15 DeviceTree infrastructure required for multi-core guest execution.
