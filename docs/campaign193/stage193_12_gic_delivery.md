# Stage 193.12 — GIC Interrupt Delivery Architecture

## 1. Objectives & Scope
Stage 193.12 audits and validates the ARM Generic Interrupt Controller (GICv3) architecture:
- Distributor (GICD) MMIO interface: `GICD_CTLR` (group enable controls), `GICD_ISENABLER` (interrupt set-enable), `GICD_ISPENDR` (interrupt set-pending), `GICD_IPRIORITYR` (priority registers), `GICD_IGROUPR` (interrupt group registers), and `GICD_IROUTER` (affinity routing).
- Redistributor (GICR) MMIO interface: per-core SGI and PPI frames with banked `igroupr0`, `isenabler0`, and `ispendr0`.
- CPU Interface (ICC) system register interface:
  - `ICC_PMR_EL1` (Priority Mask Register): priority-based interrupt masking. Interrupts with numerical priority $\ge$ PMR are masked.
  - `ICC_IGRPEN1_EL1` / `ICC_IGRPEN0_EL1`: Group 1 and Group 0 CPU interface enable controls.
  - `ICC_IAR1_EL1` (Interrupt Acknowledge Register, Group 1): reading via guest instruction `MRS X0, ICC_IAR1_EL1` (`0xD530CC00`) transitions the highest-priority pending interrupt to active and returns the INTID; subsequent reads return spurious INTID 1023.
  - `ICC_EOIR1_EL1` (End of Interrupt Register, Group 1): writing via guest instruction `MSR ICC_EOIR1_EL1, X0` (`0xD510CC20`) deactivates and completes the active interrupt.
- Per-core interrupt state isolation: Private Peripheral Interrupts (PPIs, e.g. INTID 27 CNTV virtual timer) delivered to Core 0 are exclusively recognized and acknowledged by Core 0, while other cores remain unaffected.

## 2. Implementation & Test Architecture
The test suite `tests/unit/stage193_12_gic_delivery_tests.cpp` exercises:
1. `test_distributor_and_redistributor_configuration`:
   - Configures `GICD_CTLR`, enables SPI INTID 33 via `GICD_ISENABLER1`, programs priority `0x40`, asserts SPI line, and verifies pending state in `GICD_ISPENDR1`.
2. `test_cpu_interface_priority_masking`:
   - Sets `ICC_PMR_EL1 = 0x80`.
   - SPI INTID 34 with priority `0x90` is masked by PMR (`has_interrupt == false`).
   - Reprogramming priority to `0x60` allows the interrupt to pass PMR (`has_interrupt == true`, `intid == 34`).
3. `test_interrupt_lifecycle_acknowledge_and_eoi`:
   - Configures SPI INTID 35 as Group 1 with priority `0x20` and asserts line.
   - Guest EL1 executes `MRS X0, ICC_IAR1_EL1`: returns `X0 = 35` and marks interrupt active.
   - Verification that a second read returns spurious INTID `1023`.
   - Guest EL1 executes `MSR ICC_EOIR1_EL1, X0`: deactivates interrupt, resulting in no pending interrupts.
4. `test_per_core_interrupt_isolation`:
   - Enables Core 0 and Core 1 redistributor frames.
   - Asserts PPI 27 on Core 0. Core 0 evaluates pending; Core 1 evaluates idle.
   - Core 0 acknowledges INTID 27; Core 1 receives spurious 1023.

## 3. Evidence & Verification Results
- `tests/unit/stage193_12_gic_delivery_tests.cpp` (4/4 tests passed in both Debug and Release).
- CTest target: `stage193_12_gic_delivery_tests`.
- Classification: `GUEST_INTERRUPT` / `GUEST_EL1` / `HOST_MODEL`.
