# WinIOS — Adversarial & Fault Injection Test Results
## Error Handling, Corruption Resistance & Divergence Detection

---

## 1. Adversarial Test Suite Summary

The adversarial test suite (`tests/unit/dma_adversarial_tests.cpp`) subjects the recording and replay subsystems to intentional corruption, capability tampering, and out-of-order event streams.

---

## 2. Test Results

### Group 1: Companion File Fault Injections
- **Corrupted Payload Bytes:** Bit-flipped byte inside companion `.dma` file payload.
  - *Outcome:* Detected during `coordinator.prepare()`.
  - *Divergence:* `SmpReplayDivergenceType::DmaDataMismatch`.
  - *Result:* **PASS**.
- **Truncated Companion Header:** Header truncated from 64 bytes to 16 bytes.
  - *Outcome:* Rejected during companion file open.
  - *Result:* **PASS**.
- **Payload Size Truncation:** Payload body truncated before declared length.
  - *Outcome:* Detected via CRC32C and length check.
  - *Result:* **PASS**.

### Group 2: Capability Gate Enforcement
- **`allow_dma_replay = false`:** Replaying a recording containing DMA events with DMA replay disabled.
  - *Outcome:* Coordinator halts immediately at first DMA record with `UnsupportedExternalInput`.
  - *Result:* **PASS**.
- **`allow_uncaptured_dma = true` Attempt:** Attempting to configure `allow_uncaptured_dma = true`.
  - *Outcome:* Coordinator constructor or validation rejects configuration (BLK-SMP-03 safety check).
  - *Result:* **PASS**.
- **Inconsistent Capability Combinations:** Setting `allow_dma_replay = true` while `allow_dma_recording = false`.
  - *Outcome:* Validation detects invalid matrix and rejects preparation.
  - *Result:* **PASS**.

### Group 3: Reordered & Malformed Events
- **Cyclic Descriptor Chain Injection:** Live device submits cyclic descriptor chain.
  - *Outcome:* Rejected with `VirtualDmaDescriptorStatus::ErrorCyclicChain` (status code 7); recorded as `DmaSubmissionRejected`.
  - *Result:* **PASS**.
- **Out-of-Bounds Destination Range:** Live device submits descriptor with destination beyond guest physical RAM.
  - *Outcome:* Rejected with `VirtualDmaDescriptorStatus::ErrorOutOfBounds` (status code 4); no memory corrupted.
  - *Result:* **PASS**.
- **Misaligned Descriptor Pointer:** Live device submits odd physical address (`0x10001`).
  - *Outcome:* Rejected with `VirtualDmaDescriptorStatus::ErrorMisaligned` (status code 3).
  - *Result:* **PASS**.
