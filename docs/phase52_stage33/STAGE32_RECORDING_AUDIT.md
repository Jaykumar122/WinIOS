# WinIOS — Stage 32 Recording Architecture Audit
## Comprehensive Audit of Multi-Core Recording, Companion Payload Storage & Replay Safety

**Audit Date:** November 2026  
**Auditor:** WinIOS Systems Engineering  
**Scope:** Stage 32 Implementation in `D:\WinISO26\WinIOS\`

---

## 1. Executive Findings

An exhaustive audit of the Stage 32 implementation reveals that while Stage 32 successfully established end-to-end DMA recording and zero-live-engine replay under normal, complete runs, the system currently lacks **crash consistency, cross-file session binding, up-front companion validation, and atomic recovery**.

| Subsystem / Invariant | Status in Stage 32 | Vulnerability / Deficiency | Severity |
|---|---|---|---|
| **Cross-File Binding** | UNBOUND | SNDL v2 and `.dma` files have no shared `session_id`. Files from different runs can be mixed. | **HIGH** |
| **Partial Payload Writes** | VULNERABLE | Interrupted payload write leaves unpadded or truncated entry that breaks stream parsing. | **HIGH** |
| **Missing Payload Detection** | DEFERRED | Missing payloads are detected only when consumed during replay, not during `prepare()`. | **MEDIUM** |
| **Cross-File Substitution** | UNDETECTED | A `.dma` file from another session can be loaded without header identity rejection. | **HIGH** |
| **Recording Recovery** | NOT IMPLEMENTED | Truncated/interrupted `.sndl` or `.dma` files are rejected as `Incomplete` with no recovery scanner. | **HIGH** |
| **Snapshot Continuation** | UNPROTECTED | Intermediate DMA states (mid-chain) can be snapshotted without validation of pending transactions. | **MEDIUM** |
| **Replay Checkpoints** | NOT IMPLEMENTED | No checkpointing mechanism exists to resume replay from intermediate quantum boundaries. | **MEDIUM** |

---

## 2. Component-by-Component Source Audit

### 2.1 SNDL v2 Writer and Reader
- **Files:** `src/sndl/sndl_v2_writer.h/.cpp`, `src/sndl/sndl_v2_reader.h/.cpp`
- **Audit:**
  - `SndlV2Writer` buffers records into uniform 80-byte records inside 48-byte block headers with CRC32C checksums.
  - On flush, blocks are committed to disk.
  - Finalization writes a 64-byte `WSndlV2Footer` with `SndlV2CompletionStatus::Completed` and updates `WSndlV2Header` at offset 0.
  - **Weakness:** If the process is killed before `finalize()` rewrites the header or writes the footer, the file is unfinalized (`IncompleteRecording`). `SndlV2Reader` rejects the entire file with `SndlV2ErrorCode::IncompleteRecording`. There is no scanner to recover valid records prior to the crash.

### 2.2 WDMA Companion Payload Writer and Reader
- **Files:** `src/device/dma_payload_store.h/.cpp`
- **Audit:**
  - Uses `kDmaDataFileMagic` (`0x56444D4144415441ull` = 'VDMADATA') and 64-byte file header.
  - Each payload is preceded by a 48-byte `kDmaDataEntryMagic` entry header with CRC32C over the header, followed by raw payload bytes and payload CRC32C.
  - Finalization rewrites the 64-byte header with `total_records_` and `total_bytes_`.
  - **Weakness:** The companion file header does not contain a session UUID, SNDL stream checksum, or base snapshot hash. It only stores `total_records_` and `total_bytes_`.

### 2.3 DMA Payload Index & Offset Handling
- **Audit:**
  - Currently, entries are read sequentially in `DmaPayloadStore::open_read` and loaded into an in-memory `std::unordered_map<std::uint64_t, DmaPayloadRecord> records_` keyed by `global_sequence`.
  - **Weakness:** No on-disk payload index table exists at the end of `.dma`. The file must be scanned sequentially from start to EOF to build the in-memory map. An interrupted write at the end of the file causes the entire read to fail with `TruncatedPayload`.

### 2.4 Event IDs 70–78 Semantics
- **Files:** `src/sndl/sndl_v2_types.h`, `src/device/virtual_dma_device.cpp`
- **Audit:**
  - Event 70 (`DeviceRegisterRead`), 71 (`DeviceRegisterWrite`), 72 (`DmaSubmissionAccepted`), 73 (`DmaSubmissionRejected`), 74 (`DmaTransferCompleted`), 75 (`DmaTransferFailed`), 76 (`DeviceInterruptAsserted`), 77 (`DeviceStateTransition`), 78 (`DmaDataChunk`).
  - Semantics are cleanly defined and bounded.

### 2.5 CRC32C Implementation
- **Files:** `src/trace/trace_serializer.h`, `src/trace/trace_types.h`
- **Audit:**
  - Standard Castagnoli polynomial `0x82F63B78`.
  - Used consistently across SNDL records, block headers, payload headers, and payloads.

### 2.6 Recording Session Identity
- **Audit:**
  - **DEFICIENCY:** No explicit `RecordingSessionId` exists in Stage 32. SNDL header stores `base_snapshot_hash`, but does not store a dedicated session identifier. The `.dma` file has zero cross-referencing fields.

### 2.7 Recording Finalization & Aborted-Recording Handling
- **Files:** `src/sndl/sndl_v2_recorder.cpp`
- **Audit:**
  - `SndlV2Recorder::stop_recording(status)` calls `dma_payload_store_.finalize()` followed by `writer_.finalize(status)`.
  - **Deficiency:** If an error occurs during `dma_payload_store_.finalize()`, the error is discarded with `(void)`. If `status` is `Aborted`, `dma_payload_store_` is still finalized as if it were complete.

### 2.8 Snapshot Capture and Restore (Section 0x18)
- **Files:** `src/snapshot/smp_snapshot_manager.cpp`, `src/device/virtual_dma_device.cpp`
- **Audit:**
  - Section `0x18` captures device registers: control, status, descriptor addr, interrupt ID, transfer count, bytes transferred, error code.
  - **Deficiency:** Does not verify whether a DMA transfer is currently mid-flight. If a snapshot is taken while `reg_status_ & kStatusBusy` is true, the resulting snapshot is in an intermediate state that cannot be cleanly continued without pending operation metadata.

### 2.9 Replay Event Cursor and Payload Consumption
- **Files:** `src/replay/smp_replay_coordinator.cpp`, `src/replay/smp_replay_cursor.cpp`
- **Audit:**
  - Replay consumes events from `cursor_` and queries `dma_payload_store_.find_payload(gseq)`.
  - **Deficiency:** No up-front verification in `prepare()` guarantees that every DMA Copy event in SNDL has a corresponding payload in the companion store.

---

## 3. Vulnerability Analysis & Answers to Core Inquiries

1. **Can the two files (.sndl and .dma) become inconsistent?**
   - **YES.** If recording terminates abnormally, SNDL may have flushed blocks referencing payloads that were not yet flushed to `.dma`, or vice-versa.
2. **Can a partially written payload be referenced by a valid event?**
   - **YES.** If the system crashes after writing an SNDL event block but during payload write, the event references an incomplete payload offset.
3. **Can a valid-looking SNDL stream reference missing .dma data?**
   - **YES.** In Stage 32, `SmpReplayCoordinator::prepare()` only checks that the `.dma` file exists and can be parsed, not that all referenced sequences are present.
4. **Can a .dma payload be substituted from another recording?**
   - **YES.** Because `.dma` files have no session ID or snapshot binding, substituting `other_recording.sndl.dma` will succeed during `prepare()` as long as the file is syntactically valid.
5. **Is recording recovery already implemented?**
   - **NO.** Any unfinalized file is rejected with `IncompleteRecording`.
6. **Does snapshot continuation preserve the companion-data position?**
   - **NO.** Section `0x18` does not serialize companion payload offsets or sequence positions.
7. **Can replay distinguish a complete recording from an interrupted one?**
   - **PARTIALLY.** It can check `footer.completion_status`, but cannot recover an interrupted recording up to the last valid transaction boundary.

---

## 4. Required Stage 33 Remediation Plan

1. **Phase B:** Introduce `RecordingSessionId` shared across SNDL, WDMA, and snapshots. Implement explicit recording lifecycle states and synchronized finalization.
2. **Phase C:** Add session binding and on-disk payload indexing to WDMA. Validate 100% of event-to-payload references up-front during `prepare()`.
3. **Phase D:** Implement `RecordingRecoveryScanner` to conservatively salvage valid events and payloads into an isolated recovered destination.
4. **Phase E:** Harden Section `0x18` device snapshotting: reject snapshot creation if DMA is mid-transaction; serialize session binding and payload positions.
5. **Phase F:** Implement `SmpReplayCheckpoint` to enable crash-safe replay continuation.
6. **Phase G & H:** Stress test with up to 10M DMA operations and extensive fault injection.
