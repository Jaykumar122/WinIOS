# WinIOS — SNDL v2 DMA Event Format Specification
## Binary Layout & Payload Semantics for Events 70–78

---

## 1. Event Type Enumeration

All DMA events utilize standard uniform 80-byte `SndlV2Record` binary framing in the SNDL v2 event stream.

| Event Type | ID | Description |
|---|---|---|
| `DeviceRegisterRead` | 70 | Guest read from MMIO device register. |
| `DeviceRegisterWrite` | 71 | Guest write to MMIO device register. |
| `DmaSubmissionAccepted` | 72 | Descriptor chain validated and accepted for execution. |
| `DmaSubmissionRejected` | 73 | Descriptor chain rejected due to validation or resource error. |
| `DmaTransferCompleted` | 74 | Descriptor chain executed and committed to memory. |
| `DmaTransferFailed` | 75 | Transfer failed during memory execution. |
| `DeviceInterruptAsserted` | 76 | Hardware interrupt asserted by virtual device. |
| `DeviceStateTransition` | 77 | Device operational state change (e.g., reset, disable). |
| `DmaDataChunk` | 78 | Inline 1–16 byte data chunk for small Copy operations. |

---

## 2. Detailed Field Layouts

### 2.1 `DmaSubmissionAccepted` (ID 72)
- `global_event_sequence`: Strict monotonically increasing sequence.
- `global_retirement_sequence`: Cluster retirement sequence count at submission.
- `core_retirement_count`: Retirement count of submitting core.
- `pc`: Architectural PC of submitting core.
- `core_id`: Core index (0–7).
- `source_id`: Virtual device ID (`0x1000`).
- `payload1`: Descriptor physical address (`desc_paddr`).
- `payload2`: Destination physical address (`dst_paddr`).
- `payload3`: Low 32 bits = `length_bytes`; High 32 bits = `operation_type` (0=Copy, 1=Fill).
- `flags`: Fill pattern (for Fill) or Payload CRC32C (for Copy).

### 2.2 `DmaDataChunk` (ID 78)
- `payload1`: Destination address for this chunk.
- `payload2`: First 8 bytes of payload data (little-endian).
- `payload3`: Second 8 bytes of payload data (little-endian).
- `flags`: Low 16 bits = chunk byte count (1..16).

### 2.3 `DmaTransferCompleted` (ID 74)
- `payload1`: Descriptor physical address.
- `payload2`: Total bytes transferred across submission.
- `payload3`: Total descriptors processed.
- `flags`: Status code (`0` = Success).

### 2.4 `DeviceInterruptAsserted` (ID 76)
- `core_id`: Target virtual core (or 0 for SPI routing).
- `source_id`: Originating device ID.
- `payload1`: Interrupt ID (e.g. 32).
- `payload2`: Interrupt priority (default `0x80`).
