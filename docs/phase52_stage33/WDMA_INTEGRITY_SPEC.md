# WinIOS — WDMA Companion Store Integrity Specification
## Binary Format, Cross-File Binding & Payload Integrity Checks

---

## 1. File Structure

Companion payload files (`.dma`) use the WDMA v2 layout. Large DMA buffers (descriptors, fills, copies, packet transfers) are segregated from the primary SNDL v2 event stream to preserve dense instruction cache and trace stream performance.

```
+------------------------------------------------------------------+
| Header (32 bytes)                                                |
|  - uint64_t magic               = 0x56444D4144415441 ('VDMADATA') |
|  - uint32_t version             = 2                              |
|  - uint32_t flags               = 0                              |
|  - uint64_t session_id          = 64-bit UUID                    |
|  - uint64_t base_snapshot_hash  = Canonical snapshot hash        |
+------------------------------------------------------------------+
| Payload Records (Variable count, sequential)                     |
|  Record 0:                                                       |
|   - uint64_t sequence_number   (Global event sequence)           |
|   - uint32_t channel_id                                          |
|   - uint32_t descriptor_id                                       |
|   - uint64_t payload_length                                      |
|   - uint32_t payload_crc32c                                      |
|   - uint8_t  payload_bytes[payload_length]                       |
|  Record 1 ...                                                    |
+------------------------------------------------------------------+
| Tail Index Table (Appended on finalization or recovery)          |
|  - uint64_t index_offset                                         |
|  - uint32_t total_payloads                                       |
|  - uint32_t final_state         (Complete = 1, Recovered = 2)    |
|  - Index entries: { sequence_number, offset_in_file, length }    |
+------------------------------------------------------------------+
```

---

## 2. Cross-File Identity Verification

To guarantee that a `.sndl` file and its companion `.dma` file belong to the exact same execution session:
1. **Session UUID Verification:**
   During replay initialization or recovery scanning, `sndl_session_id` must match `dma_session_id` exactly:
   $$\text{SessionID}_{\text{SNDL}} == \text{SessionID}_{\text{DMA}}$$
   If mismatched, initialization fails with `DmaPayloadStatus::SessionMismatch` or `RecoveryError::SessionMismatch`.
2. **Snapshot Hash Verification:**
   $$\text{BaseSnapshotHash}_{\text{SNDL}} == \text{BaseSnapshotHash}_{\text{DMA}}$$
   Guarantees both streams originated from the identical cluster memory and architectural state.

---

## 3. Data Integrity & Corruption Handling

Every stored payload is protected by a hardware-accelerated CRC32C checksum.
- On storage: `crc = crc32c(payload_bytes, length)`
- On read: calculated CRC is compared against `payload_crc32c`.
- If mismatch detected: `DmaPayloadStatus::CorruptedPayload` is returned; replay coordinator halts immediately and flags the exact corrupted event sequence.
