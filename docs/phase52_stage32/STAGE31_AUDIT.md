# WinIOS — Phase 52, Stage 32: Stage 31 Audit Report
## Source Code Audit of Virtual Device and DMA Infrastructure

**Auditor:** WinIOS Deterministic Architecture Agent  
**Date:** 2026-11-05  
**Stage:** Phase 52, Stage 32 (Phase A Audit)  
**Target Codebase:** `D:\WinISO26\WinIOS\`

---

## 1. Audit Objective

Prior to implementing end-to-end deterministic DMA recording and replay in Stage 32, a thorough audit of the actual source code implemented in Stage 31 was conducted. The goal was to verify what is actually functional versus what was merely reserved or prototyped, and to identify the exact technical requirements needed to achieve genuine replay without live engine execution or reading live source memory.

---

## 2. Component-by-Component Source Code Audit

### 2.1 VirtualDeviceBus (`src/device/virtual_device_bus.h`, `src/device/virtual_device_bus.cpp`)
- **Actual Implementation:**
  - Manages a list of `VirtualDevice` instances.
  - Correctly verifies address overlap on registration (`std::max(start1, start2) < std::min(end1, end2)`).
  - Handles MMIO reads and writes with size validation (1, 2, 4, 8 bytes) and strict alignment checking (`offset % width == 0`). Misaligned accesses return `VirtualDeviceAccessResult::MisalignedAccess`.
  - Connects device interrupt callbacks to `SmpInterruptRouter::request_interrupt(0, irq_line, 0x80u)`.
  - Has optional pointer to `SndlV2Recorder`, recording `DeviceRegisterRead` and `DeviceRegisterWrite` events.
- **Audit Finding:**
  - Fully functional as an MMIO bus.
  - However, interrupt line routing is hardcoded to core 0 in the default callback; multi-core targeted interrupt routing requires explicit device configuration.

### 2.2 VirtualDmaEngine (`src/device/virtual_dma_engine.h`, `src/device/virtual_dma_engine.cpp`)
- **Actual Implementation:**
  - Executes descriptor chains synchronously on the calling thread.
  - Validates descriptor alignment (8-byte aligned addresses), length limits (`length_bytes <= max_transfer_bytes`), 64-bit address overflow (`paddr + length < paddr`), and memory bounds against guest RAM capacity.
  - Validates chain depth (up to 256 descriptors) and detects cyclic links via a visited address vector.
  - Executes `Copy` (read source, write destination) and `Fill` (replicate 32-bit pattern to destination).
  - Memory writes invoke `SmpSharedMemory::write_bytes`, which automatically triggers `SmpExclusiveMonitorManager::invalidate_reservations`.
  - Updates descriptor `status` in guest RAM upon completion or error.
- **Audit Finding:**
  - The engine executes directly against live memory without awareness of SNDL recording.
  - It does NOT capture source data prior to execution for replay purposes.
  - During replay, invoking `VirtualDmaEngine::execute_descriptor_chain` is strictly prohibited by Stage 32 requirements; a dedicated replay application path is needed.

### 2.3 VirtualDmaDevice (`src/device/virtual_dma_device.h`, `src/device/virtual_dma_device.cpp`)
- **Actual Implementation:**
  - Memory-mapped controller prototype at `0x40000000` (registers: `kControl`, `kStatus`, `kDescriptorAddr`, `kTransferCount`, `kBytesTransferred`, `kErrorCode`, `kInterruptId`, `kDeviceId`).
  - Contains embedded `VirtualDmaEngine`.
  - Provides `attach_sndl_recorder(sndl::SndlV2Recorder*)`.
  - `execute_submission` calls `engine_.execute_descriptor_chain` and updates status/registers.
- **Audit Finding:**
  - In Stage 31, `execute_submission` did NOT invoke `sndl_recorder_`! Even though `sndl_recorder_` could be attached, DMA submission and transfer events were not emitted during live device register writes to `kControlStart`.
  - Stage 32 must wire the 7-step pre-commit and post-commit recording sequence into `VirtualDmaDevice` and `VirtualDmaEngine`.

### 2.4 DMA Descriptor Structures (`src/device/virtual_dma_types.h`)
- **Actual Implementation:**
  - `VirtualDmaDescriptor` is exactly 64 bytes (`alignas(8)`).
  - Contains `descriptor_id`, `src_paddr`, `dst_paddr`, `length_bytes`, `operation_type`, `flags`, `status`, `error_code`, `fill_pattern`, and `next_descriptor_paddr`.
- **Audit Finding:**
  - Complete, robust, and correctly aligned.

### 2.5 SNDL v2 Event IDs 70–77 (`src/sndl/sndl_v2_types.h`)
- **Actual Implementation:**
  - IDs 70–77 are declared in `SndlV2EventType`:
    - 70: `DeviceRegisterRead`
    - 71: `DeviceRegisterWrite`
    - 72: `DmaSubmissionAccepted`
    - 73: `DmaSubmissionRejected`
    - 74: `DmaTransferCompleted`
    - 75: `DmaTransferFailed`
    - 76: `DeviceInterruptAsserted`
    - 77: `DeviceStateTransition`
  - Helper methods exist in `SndlV2Recorder`: `record_dma_submission_accepted/rejected` and `record_dma_transfer_completed/failed`.
- **Audit Finding:**
  - The existing recording methods in Stage 31 only stored metadata (address, length, op_type, bytes_transferred) in uniform 80-byte `SndlV2Record` entries (`payload1`, `payload2`, `payload3`).
  - **Critical gap:** For `Copy` operations, the actual source bytes were NOT captured anywhere! Without capturing source bytes or providing a companion data mechanism, replay cannot reproduce the memory changes without reading live source memory.

### 2.6 SNDL v2 Writer & Reader (`src/sndl/sndl_v2_writer.*`, `src/replay/smp_replay_reader.*`)
- **Actual Implementation:**
  - Block-based format with 64-byte Header, 48-byte Block Header, 80-byte uniform records with CRC32C, and 64-byte Footer.
  - Reader streams blocks sequentially, verifies CRC32C of blocks and records, and enforces monotonic sequence progression.
- **Audit Finding:**
  - Rock solid and verified. To support arbitrary-length DMA payload data without breaking the 80-byte uniform record format, we must either:
    1. Chunk data into bounded SNDL records (e.g. event ID 78: `DmaDataChunk`), and/or
    2. Provide a companion data binary file (`<file>.sndl.dma`) referenced by global sequence.
    Supporting both ensures small transfers are self-contained in the SNDL stream while large transfers (up to 16 MiB) have zero buffer bloat.

### 2.7 WSnap v2 Device-State Section (`src/snapshot/smp_snapshot_format.*`, `snapshot_types.h`)
- **Actual Implementation:**
  - Section ID `0x18u` (`DeviceState`).
  - Serializes `device_id` (4B), `payload_size` (4B), payload data, and CRC32C (4B).
  - Deserialization validates CRC32C.
  - `VirtualDmaDevice` serializes its 64-byte internal state (`magic`, control, status, descriptor addr, transfer count, bytes transferred, error code, interrupt ID, CRC32C).
- **Audit Finding:**
  - Section serialization and deserialization are implemented and unit tested.
  - However, in `SmpSnapshotManager`, Section 0x18 was not automatically populated from the running cluster because `VirtualDeviceBus` was not yet registered in `SmpScheduler`!
  - Stage 32 must connect `VirtualDeviceBus` to `SmpScheduler` and `SmpSnapshotManager` so taking a snapshot automatically captures all registered virtual devices.

### 2.8 SMP Replay Coordinator & Event Cursor (`src/replay/smp_replay_coordinator.*`, `smp_replay_cursor.*`)
- **Actual Implementation:**
  - `process_pending_external_events()` peeks events at each scheduler quantum boundary.
  - In Stage 31:
    ```cpp
    case sndl::SndlV2EventType::DmaSubmissionAccepted:
    case sndl::SndlV2EventType::DmaSubmissionRejected:
    case sndl::SndlV2EventType::DmaTransferCompleted:
    case sndl::SndlV2EventType::DmaTransferFailed: {
        if (!config_.allow_dma_replay) {
            trigger_divergence(...);
            return false;
        }
        sndl::SndlV2Record consumed{};
        cursor_.consume_expected_type(rec.event_type, consumed);
        stats_.total_events_consumed++;
        break;
    }
    ```
- **Audit Finding:**
  - In Stage 31, DMA events were merely consumed and counted; their memory effects, device state transitions, and interrupt consequences were NOT applied during replay!
  - Stage 32 must replace this stub with genuine replay application: writing recorded destination memory, invalidating exclusive reservations, updating device status registers, and firing interrupts when recorded.

### 2.9 Shared Guest Memory & Exclusive-Monitor Invalidation (`src/cpu/smp_shared_memory.*`, `smp_exclusive_monitor.*`)
- **Actual Implementation:**
  - `SmpSharedMemory::write_bytes` automatically notifies `SmpExclusiveMonitorManager::invalidate_reservations` across all cores for the affected 64-byte granules.
- **Audit Finding:**
  - Fully functional. Any replay write to `SmpSharedMemory` will correctly preserve exclusive-monitor consistency across virtual cores.

### 2.10 Existing DMA Capability Gates (`src/replay/smp_replay_types.h`)
- **Actual Implementation:**
  - `allow_virtual_devices = false`
  - `allow_virtual_dma = false`
  - `allow_dma_recording = false`
  - `allow_dma_replay = false`
  - `allow_uncaptured_dma = false` (strictly rejected on prepare)
- **Audit Finding:**
  - Capability gates exist. DMA replay is disabled by default. Enabling `allow_dma_replay` without `allow_dma_recording` is rejected.
  - These invariants must be preserved.

---

## 3. Required Data to Reproduce DMA Operations During Replay

To reproduce DMA transfers deterministically during replay without calling `VirtualDmaEngine::execute()` and without reading live source memory:

1. **For `VirtualDmaOpType::Fill`:**
   - Destination address (`dst_paddr`)
   - Length in bytes (`length_bytes`)
   - 32-bit Fill pattern (`fill_pattern`)
   - Target device ID
   - Interrupt enable flag and line
   *Replay action:* Replicate the 32-bit fill pattern into `SmpSharedMemory` at `dst_paddr` for `length_bytes`, invalidate exclusive monitors on affected granules, update device registers, and signal interrupt if flagged.

2. **For `VirtualDmaOpType::Copy`:**
   - Destination address (`dst_paddr`)
   - Length in bytes (`length_bytes`)
   - **Recorded source payload data** (captured at recording time before memory commit)
   - Target device ID
   - Interrupt enable flag and line
   *Replay action:* Write the recorded source bytes directly into `SmpSharedMemory` at `dst_paddr` for `length_bytes`, invalidate exclusive monitors, update device registers, and signal interrupt if flagged.

3. **For Rejected / Failed Submissions:**
   - Descriptor address
   - Error code (`VirtualDmaDescriptorStatus`)
   - Target device ID
   *Replay action:* Update device status register (`kStatusError`, `kErrorCode`), leave destination memory completely unaltered, and do not advance cursor until validation succeeds.

---

## 4. Conclusion & Action Plan for Stage 32

Stage 31 established the types, bus, engine, and descriptors, but recording was decoupled from live execution and replay only discarded events without applying memory changes.

Stage 32 will implement:
1. Complete DMA data capture and recording in `VirtualDmaDevice` / `VirtualDmaEngine`.
2. Companion DMA payload storage and bounded `DmaDataChunk` records.
3. Genuine DMA replay in `SmpReplayCoordinator` applying recorded effects to `SmpSharedMemory` without live engine execution or source memory reads.
4. Automatic `DeviceState` snapshot capture and restoration in `SmpScheduler` / `SmpSnapshotManager`.
5. Comprehensive test suites covering 1-, 2-, 4-, and 8-core topologies, repeated deterministic runs, and adversarial corruptions.
