# WinIOS — DMA Recording Specification
## Deterministic DMA Capture & Companion Payload Storage

---

## 1. Specification Overview

This specification defines the capture semantics, event ordering, and companion storage protocols for recording virtual DMA transfers within the SNDL v2 framework.

---

## 2. Event Capture Requirements

### 2.1 Event Sequence
Every virtual DMA transfer generates a deterministic event sequence:
1. `DmaSubmissionAccepted` (ID 72):
   - `core_id`: Submitting core index.
   - `source_id`: Device ID (default `0x1000`).
   - `payload1`: Descriptor guest physical address (`desc_addr`).
   - `payload2`: Destination guest physical address (`dst_paddr`).
   - `payload3`: Low 32 bits = `length_bytes`; High 32 bits = `operation_type` (0=Copy, 1=Fill).
   - `flags`: Fill pattern (for Fill) or Payload CRC32C (for Copy).
2. For Copy transfers:
   - If `length_bytes <= 16`: Emits one or two `DmaDataChunk` (ID 78) inline records.
   - If `length_bytes > 16`: Writes raw source bytes to companion `.dma` file keyed by `global_event_sequence`.
3. `DmaTransferCompleted` (ID 74):
   - `core_id`: Submitting core index.
   - `payload1`: Descriptor guest physical address.
   - `payload2`: Total bytes committed.
   - `payload3`: Total descriptors processed.
4. `DeviceInterruptAsserted` (ID 76, optional):
   - Emitted if `kDmaFlagInterruptOnComplete` or `kControlIrqEnable` is set.
   - `payload1`: Interrupt ID (e.g., 32).

### 2.2 Pre-Commit Invariant
Source data must be read and staged into memory **before** writing any destination bytes. This ensures that even if `src_paddr` and `dst_paddr` overlap, the recorded source payload represents the original unmutated data.

---

## 3. Companion File Protocol (`.dma`)

A companion data store with the extension `<recording>.sndl.dma` stores copy payloads that exceed inline thresholds.
- **Magic:** `WDMA` (4 bytes, `0x57, 0x44, 0x4D, 0x41`).
- **Version:** `1` (uint16_t).
- **Header:** 64 bytes total, protected by CRC32C.
- **Record Framing:**
  - Header: 40 bytes (Sequence, Descriptor ID, Dst Paddr, Length, CRC32C).
  - Data: Bounded payload bytes `[0..length)`.
  - Alignment: 8-byte padded.
