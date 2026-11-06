# WinIOS — Phase 52, Stage 32 Architecture
## End-to-End Deterministic DMA Recording & Replay Architecture

---

## 1. Architectural Overview

Stage 32 integrates virtual DMA operations into the WinIOS deterministic SMP recording and replay architecture. The design addresses the core challenge of deterministic replay: **external device memory modifications must be recorded and reproduced without re-running the nondeterministic device logic or depending on unmodified source buffers.**

```
Live Execution (Recording):
[Guest Core / MMIO Write]
          │
          ▼
 [VirtualDmaDevice] ──(Pre-commit read)──► [Shared RAM Source]
          │                                        │
          ├──► Capture Payload ────────────────────┼──► [.dma Companion Store]
          │
          ├──► Record SndlV2 Events ───────────────┼──► [.sndl v2 Log]
          │
          ▼ (Commit write)
   [Shared RAM Dst] ──(Invalidate)──► [Exclusive Monitor Manager]
          │
          ▼
   [SmpInterruptRouter] (IRQ assertion)

Replay Execution:
       [.sndl v2 Log] + [.dma Companion Store]
              │
              ▼
   [SmpReplayCoordinator] (Zero Live Engine Execution)
              │
              ├──► Validate CRC32C of stored payloads
              │
              ├──► Write stored bytes directly to [Shared RAM Dst]
              │
              ├──► Invalidate reservations on [Exclusive Monitor Manager]
              │
              └──► Inject asserted IRQs to [SmpInterruptRouter]
```

---

## 2. Component Pipeline

### 2.1 VirtualDmaDevice 7-Step Recording Pipeline
When `kControlStart` is written to register `0x00`:
1. **Descriptor Chain Validation:** Verifies alignment (8-byte), bounded length (<= 64 KiB), chain depth (<= 16), and absence of cycles.
2. **Memory Range Check:** Confirms descriptor, source, and destination physical ranges fall within configured guest physical RAM.
3. **Pre-Commit Payload Capture:** Reads source bytes before committing destination writes, avoiding self-overwrite corruption in overlapping ranges.
4. **Recording Buffer Capacity Check:** Verifies SNDL v2 ring buffers have sufficient capacity; drops and rejects submission if full.
5. **Memory Transaction Commit:** Commits destination writes to `SmpSharedMemory`.
6. **Exclusive Monitor Invalidation:** Clears exclusive reservations across all cores holding reservations within the target granule `[dst_paddr, dst_paddr + length)`.
7. **Deterministic Event Serialization:** Emits `DmaSubmissionAccepted` (Event 72), copy payload records to companion `.dma` file (or `DmaDataChunk` inline for <= 16 bytes), `DmaTransferCompleted` (Event 74), and optional `DeviceInterruptAsserted` (Event 76).

### 2.2 Replay Execution Pipeline (`SmpReplayCoordinator`)
During replay:
- Live `VirtualDmaEngine::execute` is **never** invoked.
- Live source memory is **never** read.
- When `cursor_.peek_next_record()` encounters a DMA event:
  - `DmaSubmissionAccepted` (Fill): Generates pattern directly into guest RAM at `dst_paddr`.
  - `DmaSubmissionAccepted` (Copy): Retrieves binary payload from `DmaPayloadStore` keyed by `global_event_sequence`, verifies its CRC32C, and writes bytes directly to `dst_paddr`.
  - `DmaDataChunk`: Extracts inline 16-byte chunk and writes to guest RAM.
  - `DmaTransferCompleted`: Updates descriptor status field to `VirtualDmaDescriptorStatus::Success` in guest RAM.
  - `DeviceInterruptAsserted`: Dispatches interrupt directly into `SmpInterruptRouter`.
  - Reservations in `ExclusiveMonitorManager` are cleared across all cores for the modified range.
- If payload verification fails or capabilities are violated, divergence is triggered immediately (`SmpReplayDivergenceType::DmaDataMismatch`).

---

## 3. Synchronization & Determinism

1. **Global Event Monotonicity:** DMA submission and completion events occupy discrete points in the global event sequence `global_event_sequence`.
2. **Retirement Counter Correlation:** External DMA events are consumed at the exact quantum boundaries and retirement sequence recorded.
3. **Core Independence:** Any core can trigger DMA; the resulting memory effects and interrupts are reproducible identically regardless of scheduling order.
