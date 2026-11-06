# WinIOS Phase 52 Stage 34: Independent Recording Recovery Validation

## 1. Overview & Verification Scope

The Independent Recording Recovery suite (`tests/unit/independent_recording_recovery_tests.cpp`) tests the crash-consistency and stream-salvage capabilities of `winios::replay::DmaRecordingRecoveryManager` without using production writers.

All test inputs are generated as raw binary byte vectors with independent CRC32C computation. This guarantees that recovery routines are tested against genuine corruptions, structural tears, companion file mismatches, and orphan records rather than artifacts of internal serialization helpers.

---

## 2. Tested Scenarios & Invariant Proofs

### Test 1: Header-Only Recording Salvage
- **Fixture:**
  - 64-byte `WSndlV2Header` with clean magic (`WSNDL2\0`), stream version 2, and initial cluster hash. Zero event blocks appended (total file size exactly 64 bytes).
- **Behavior:**
  - `recover_stream()` inspects header, validates magic and CRC, recognizes valid zero-event recording.
- **Result:**
  - Status: `RecordingRecoveryStatus::Success` / `SalvagedHeaderOnly`.
  - Input stream left unmodified. Output stream size is 64 bytes.
  - Invariant verified: No spurious records injected.
- **Status:** **PASS**

### Test 2: Interrupted SNDL Block & Partial Trailing Record
- **Fixture:**
  - Valid SNDL header + 1 complete 80-byte event block (CRC32C valid).
  - 1 partial trailing block truncated at 37 bytes (simulating sudden power loss or process kill mid-write).
- **Behavior:**
  - Recovery manager scans block chains, detects partial block, identifies invalid block length and CRC mismatch.
  - Truncates stream to boundary of last valid block (offset 144).
- **Result:**
  - Clean salvage of block 1; partial block safely discarded.
  - Recovered stream passes `SndlV2Reader::validate()`.
- **Status:** **PASS**

### Test 3: Companion Session UUID Mismatch Rejection
- **Fixture:**
  - Valid SNDL v2 stream with Session UUID `A` (`11111111-1111-1111-1111-111111111111`).
  - Companion WDMA payload store with Session UUID `B` (`22222222-2222-2222-2222-222222222222`).
- **Behavior:**
  - Recovery manager validates both files individually, then verifies cryptographic session binding.
  - Rejects the paired session due to UUID mismatch.
- **Result:**
  - Status: Structured error `DmaPayloadStoreError::SessionIdMismatch`.
  - Prevents cross-session DMA payload contamination.
- **Status:** **PASS**

### Test 4: Trailing Orphan DMA Begin Trimming Invariant
- **Fixture:**
  - SNDL stream containing a completed DMA operation (`DmaDescriptorRead` -> `DmaTransferProgress` -> `DmaDescriptorWritten`).
  - Stream terminates with an uncommitted `DmaDescriptorRead` (Event ID 70) and unclosed quantum (Event ID 41 without 42).
- **Behavior:**
  - Recovery manager detects trailing open DMA transaction without corresponding completion record.
  - Trims event stream back to the last committed transaction boundary.
- **Result:**
  - Open DMA submission and incomplete quantum cleanly rolled back.
  - Final stream invariants verified: Every `QuantumStart` has matching `QuantumEnd`; no orphan DMA operations remain.
- **Status:** **PASS**

### Test 5: End-to-End Recovery & Replay Roundtrip
- **Fixture:**
  - Completely hand-crafted binary `.wsnap v2` snapshot (1 core, initial PC = `0x1000`).
  - Completely hand-crafted binary `SNDL v2` stream (CoreSelected, QuantumStart, 10 instructions, QuantumEnd).
  - Stream manually padded with 17 trailing corrupt garbage bytes (`0xDE, 0xAD, 0xBE, 0xEF...`).
- **Behavior:**
  - Recovery manager repairs stream, strips garbage bytes, validates block headers.
  - Stream passed into `SmpReplayCoordinator` with capability config.
- **Result:**
  - Replay executes to stream completion with 0 errors.
  - Final global retirement sequence matches recorded value exactly (10 instructions).
- **Status:** **PASS**

---

## 3. Recovery Invariant Matrix

| Invariant | Description | Verification Method | Result |
|---|---|---|---|
| **INV-REC-01** | Zero Input Mutation | SHA256 of source file verified before and after recovery | **VERIFIED** |
| **INV-REC-02** | Atomic Truncation | No partial records present in recovered output | **VERIFIED** |
| **INV-REC-03** | Monotonic Sequences | Global sequence numbers in recovered stream strictly increasing | **VERIFIED** |
| **INV-REC-04** | Companion Binding | Mismatched WDMA files rejected before replay initialization | **VERIFIED** |
| **INV-REC-05** | Closed Quantums | Every salvaged `QuantumStart` has a corresponding `QuantumEnd` | **VERIFIED** |
