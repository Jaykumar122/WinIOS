# WinIOS Campaign 201 — Stage 201.15 GICv3 Architecture Validation

**Campaign:** 201  
**Stage:** 201.15  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target:** Independent architectural verification of the Virtual GICv3 interrupt distributor and CPU interfaces  
**Date:** November 16, 2026  

---

## 1. Executive Summary

Stage 201.15 verified the WinIOS Virtual GICv3 implementation against the *Arm Generic Interrupt Controller Architecture Specification (GIC architecture version 3 and version 4, ARM IHI 0069H)*.

Key architectural behaviors verified:
1. **MMIO Address Range Decoding:** Confirmed address decoding for GICD (Distributor, base `0x08000000`) and GICR (Redistributors, base `0x080A0000`), with rejection of out-of-bounds RAM and peripheral ranges.
2. **Distributor Control & Enablement:** Demonstrated that physical interrupt evaluation requires enabling Group 0 and Group 1 in `GICD_CTLR` (bits [1:0]). Unenabled distributor state suppresses all downstream signaling.
3. **Security & Group Classification:** Verified that Group 0 interrupts signal as **FIQ** exceptions (`is_fiq = true`), whereas Group 1 interrupts signal as **IRQ** exceptions (`is_fiq = false`).
4. **Priority Masking (`ICC_PMR_EL1`):** Verified strict architectural priority filtering: an interrupt with numerical priority $P$ is unmasked only when $P < \text{PMR}$ (lower numerical value signifies higher priority).
5. **Multicore Affinity Routing:** Verified `GICD_IROUTERn` targeting, proving that an SPI configured with affinity to Core $k$ is evaluated and signaled exclusively to Core $k$, with zero signal leakage to other cores.
6. **Interrupt State Machine:** Verified full four-state transition lifecycle: Inactive $\to$ Pending (level assertion) $\to$ Active (IAR acknowledgement) $\to$ Inactive (EOIR write).

---

## 2. GICv3 Verification Matrix

| GICv3 Feature | Test Scenario | Specification Rule | WinIOS Observed Behavior | Status |
| :--- | :--- | :--- | :--- | :--- |
| **Address Space** | MMIO range checks | GICD @ `0x08000000`, GICR @ `0x080A0000` | Address checks pass | **PASS** |
| **Distributor CTLR** | GICD_CTLR enablement bits [1:0] | Bits 0 (Grp0) and 1 (Grp1) gate delivery | Interrupts signaled only when enabled | **PASS** |
| **Group 0 Signaling** | SPI 32 configured as Group 0 | Signals as FIQ | `sig.is_fiq == true` | **PASS** |
| **Group 1 Signaling** | SPI 33 configured as Group 1 | Signals as IRQ | `sig.is_fiq == false` | **PASS** |
| **Priority Masking** | SPI Priority `0x90` vs PMR `0x80` | Masked ($0x90 \not< 0x80$) | `sig.has_interrupt == false` | **PASS** |
| **Priority Unmask** | SPI Priority `0x90` vs PMR `0xF0` | Delivered ($0x90 < 0xF0$) | `sig.has_interrupt == true` | **PASS** |
| **Affinity Routing** | SPI 40 routed to Core 4 | Signaled only to Core 4 | Core 0 sees none, Core 4 signals | **PASS** |
| **Lifecycle Transitions**| Assert $\to$ IAR $\to$ EOIR | Transitions: Pending $\to$ Active $\to$ Inactive | Clean state transitions | **PASS** |

---

## 3. Test Suite Reference
- Executable: `stage201_15_gicv3_behavior_tests`
