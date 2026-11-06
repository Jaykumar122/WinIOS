# Interrupt Validation & Queue Isolation Results
**Cross-Core SGI, PPI, and SPI Routing Validation**

---

## 1. Interrupt Classification

WinIOS classifies and manages interrupts across three distinct architectural categories:
1. **Software Generated Interrupts (SGIs, INTID 0–15):** Banked per-core. Generated via `ICC_SGI1R_EL1`. Can target individual cores or multiple affinity targets via bitmasks.
2. **Private Peripheral Interrupts (PPIs, INTID 16–31):** Banked per-core (e.g., Virtual Timer IRQ 27, Hypervisor Timer IRQ 26).
3. **Shared Peripheral Interrupts (SPIs, INTID 32–1019):** Global peripherals (e.g., Virtual DMA IRQ 48, UART IRQ 33). Routed to cores via `GICD_IROUTERn`.

---

## 2. Multi-Core Routing Isolation Results

In `gic_memory_ordering_tests` Test 2:
- An 8-core topology was configured (`SmpInterruptRouter(8)`).
- **Core 0 PPI Injection:** PPI 27 was asserted targeting Core 0.
- **Multicast SGI Injection:** SGI 3 was triggered by Core 0 targeting Cores 1 and 2 (`TargetList = 0b0110`).
- **Isolation Verification:**
  - Core 0 queue count: `1` (INTID 27).
  - Core 1 queue count: `1` (INTID 3).
  - Core 2 queue count: `1` (INTID 3).
  - Cores 3–7 queue counts: `0`.
- **Conclusion:** Core queues remain strictly isolated; interrupts intended for specific cores never cross-contaminate unrelated cores.
