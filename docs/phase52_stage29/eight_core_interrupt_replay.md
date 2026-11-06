# Eight-Core Interrupt Routing & Injection Replay

## 1. Cross-Core and External Interrupt Architecture
The `SmpInterruptRouter` manages interrupt delivery across 8 cores:
- PPI (Private Peripheral Interrupts) targeting specific core IDs (0–7).
- SPI (Shared Peripheral Interrupts) routed to target cores according to affinity configuration.
- SGI (Software Generated Interrupts) routed between virtual cores for IPI signaling.

## 2. Replay Injection Loop
External and cross-core interrupts are recorded in SNDL v2 with:
- `ExternalInterruptAsserted`: Interrupt ID, triggering core, vector.
- `InterruptRouted`: Target core ID and priority.
- `InterruptAccepted`: Exact instruction retirement boundary where the target core accepted the exception.

During replay, the coordinator injects interrupts at the exact recorded retirement boundary, ensuring interrupt handling routines execute at identical program counters.
