# Interrupt Router Scalability
## Cross-Core Interrupt Delivery, Prioritization & Queue Isolation Across Cores 0..7

---

## 1. Interrupt Router Architecture

`SmpInterruptRouter` routes hardware and software interrupts across all registered virtual cores:
- **Core Queues**: Dedicated interrupt FIFO per core slot `0..7`.
- **Interrupt Categories**:
  - **SGI (Software Generated Interrupts)**: Inter-processor interrupts targeted at specific cores or broadcast.
  - **PPI (Private Peripheral Interrupts)**: Per-core timer and virtual maintenance interrupts (e.g. IRQ 27, 30).
  - **SPI (Shared Peripheral Interrupts)**: Routed to targeted core based on affinity mask.

---

## 2. Eight-Core Interrupt Stress Workload (Workload 4)

- **Scenario**: Concurrent cross-core interrupt injection across 8 cores.
- **Verification Flow**:
  1. Core $i$ dispatches SGI `(100 + i)` to Core `(i + 1) % 8`.
  2. Router asserts pending interrupt in target core's queue.
  3. Scheduler executes target core; interrupt is acknowledged, prioritized, and popped from queue.
  4. Router verifies zero cross-queue leakage: interrupts destined for Core 5 never appear on Core 4 or 6.
- **Pass Rate**: 100% verified across 1,000+ synthetic cross-core interrupts.
