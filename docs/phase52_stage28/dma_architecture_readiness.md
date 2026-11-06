# DMA Architecture Readiness
## Evaluation of BLK-SMP-03, Capability Gate Protection & Host Memory Interop

---

## 1. Blocker Status: BLK-SMP-03

| Blocker ID | Severity | Description | Current Status |
|---|---|---|---|
| **BLK-SMP-03** | HIGH | Asynchronous host DMA writes into guest memory are not captured or virtualized | **ACTIVE / MITIGATED VIA CAPABILITY GATE** |

---

## 2. Capability Gate Protection

In WinIOS Stage 28:
- No asynchronous DMA hardware peripherals (such as PCI NVMe controllers, GPU command rings, or hardware NIC ring buffers) are currently instantiated in the emulator.
- All guest memory mutations occur strictly synchronously on the single host thread via CPU instructions or synchronous MMIO handlers.
- **Capability Enforcement**:
  - `allow_uncaptured_dma` in `SmpTopologyConfig` and replay engine configuration defaults to `false`.
  - Setting `allow_uncaptured_dma = true` is explicitly rejected with `ReplayErrorCode::UnsupportedConfiguration`.
  - This capability gate guarantees that no uncaptured, asynchronous host-to-guest memory transfers can introduce non-determinism into the replay pipeline.
