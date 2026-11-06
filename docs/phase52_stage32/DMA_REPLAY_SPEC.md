# WinIOS — DMA Replay Specification
## Deterministic DMA Reproduction & Divergence Detection

---

## 1. Objective and Invariants

The DMA replay subsystem reproduces the exact memory and interrupt state transitions caused by virtual DMA during live execution without live hardware/device emulation.

### Mandatory Replay Invariants
1. **Zero Live Engine Invocation:** `VirtualDmaEngine::execute_descriptor_chain` or `VirtualDmaEngine::execute` must **never** be invoked during replay.
2. **Zero Live Source Reads:** Guest memory at `src_paddr` must **never** be read to obtain transfer data during replay.
3. **Atomic Destination Injection:** Data must be injected from the recorded event record (`fill_pattern` or `DmaDataChunk`) or companion payload store (`.dma`).
4. **Immediate Divergence Halting:** Replay must abort at the first byte mismatch or corrupt record.

---

## 2. Replay State Machine

```
               [SmpReplayCoordinator::prepare]
                             │
            Load .wsnap v2 (incl. Section 0x18)
            Open .sndl v2 & Load Companion .dma
                             │
                             ▼
                         [Ready]
                             │
                             ▼
                    [Execute Quantum]
                             │
              Peek Cursor: DMA Event Pending?
                   ├── No  ──► Continue core step
                   └── Yes ──► Dispatch DMA Replay:
                                ├── Fill: write pattern to dst_paddr
                                ├── Copy: lookup .dma -> verify CRC32C -> write to dst_paddr
                                ├── Chunk: write 16 bytes inline to dst_paddr
                                ├── Update descriptor status (0x28 offset)
                                ├── Invalidate exclusive reservations
                                └── Assert IRQ via SmpInterruptRouter
                             │
                             ▼
               Canonical Cluster Hash Verification
                             │
                             ▼
                        [Completed]
```

---

## 3. Divergence Classification

| Divergence Type | Enum Value | Description |
|---|---|---|
| `UnsupportedExternalInput` | 13 | Replay encountered a DMA event while `allow_dma_replay` is disabled. |
| `DmaDataMismatch` | 17 | Companion payload CRC32C failure, truncated payload file, or memory divergence. |
| `ExternalInputMismatch` | 11 | Unexpected DMA event sequence or core index mismatch. |
