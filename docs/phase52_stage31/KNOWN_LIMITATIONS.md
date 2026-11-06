# WinIOS — Phase 52, Stage 31 Known Limitations

## 1. Scope & Intentional Boundaries
Stage 31 delivers the device virtualization and virtual DMA architectural foundation. It intentionally avoids enabling unconstrained DMA replay or hardware passthrough.

## 2. Identified Limitations

| Limitation ID | Severity | Description | Current Mitigation / Roadmap |
|---|---|---|---|
| LIM-DEV-01 | LOW | Only memory-to-memory virtual DMA is implemented; device-to-memory or memory-to-device transfers are not yet modeled. | Memory-to-memory covers initial DMA virtualization needs; peripheral FIFO bridges scheduled for Phase 53. |
| LIM-DEV-02 | LOW | MMIO bus currently uses linear vector scanning for registered peripherals. | Peripheral counts in virtual machine setups are small (< 32); radix tree / interval tree lookup planned if > 64 devices are needed. |
| LIM-DEV-03 | MEDIUM | Scatter-gather chains limited to 256 descriptors per submission. | Prevents denial-of-service or infinite loops on guest-generated cyclic descriptors; configurable via `VirtualDmaLimits`. |
| LIM-DEV-04 | LOW | Single synthetic device (`0x40000000`, VDMA) currently instantiated by default. | Architecture allows arbitrary new peripherals to inherit from `VirtualDevice` and attach to `VirtualDeviceBus`. |

## 3. Active Blocker Status
- **BLK-SMP-03 (Asynchronous Host DMA & I/O):**
  - **Status:** **ACTIVE / MITIGATED**
  - **Rationale:** Virtual DMA is fully synchronous and confined to guest RAM on the scheduler thread, eliminating host concurrency race conditions. DMA replay capability is safely gated off (`allow_dma_replay = false`, `allow_uncaptured_dma = false`). True deterministic DMA replay execution will be unlocked in Stage 32.
