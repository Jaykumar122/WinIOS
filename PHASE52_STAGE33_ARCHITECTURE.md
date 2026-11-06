# WinIOS — Phase 52, Stage 33 Architecture Specification
## Crash-Consistent Recording, Snapshot Continuation & Replay Recovery

---

## 1. Architectural Overview

Stage 33 establishes the formal persistence resilience layer for WinIOS deterministic execution, multi-core SNDL v2 recording, and companion DMA payload stores.

```
       +-------------------------------------------------------+
       |                  WinIOS Host Process                  |
       |                                                       |
       |  +--------------------+      +---------------------+  |
       |  |  SmpScheduler      |      | VirtualDmaDevice    |  |
       |  |  (Cores 0..7)      |      | (Scatter/Gather)    |  |
       |  +---------+----------+      +----------+----------+  |
       |            |                            |             |
       |            v                            v             |
       |  +--------------------+      +---------------------+  |
       |  | SndlV2Recorder     |=====>| DmaPayloadStore     |  |
       |  | - Session UUID     |sync  | - Session UUID      |  |
       |  | - Block CRC32C     |bind  | - Payload CRC32C    |  |
       |  +---------+----------+      +----------+----------+  |
       +------------|----------------------------|-------------+
                    |                            |
          Write flush boundaries       Atomic Payload append
                    |                            |
                    v                            v
       +-------------------------+    +------------------------+
       |   session.sndl (v2)     |    | session.sndl.dma (v2)  |
       | - Stream Lifecycle:     |    | - Magic: 'VDMADATA'    |
       |   Complete / Incomplete |    | - Tail Index Table     |
       +-------------------------+    +------------------------+
                    \                            /
                     \                          /
            [CRASH / INTERRUPTION / POWER CUT OCCURS]
                         \                  /
                          v                v
       +-------------------------------------------------------+
       |               RecordingRecoveryScanner                |
       | - Forward sweep validating block & payload CRCs       |
       | - Cross-file Session UUID & Snapshot Hash binding     |
       | - Conservative backward trim to last clean boundary   |
       |   (Drop uncompleted DMA transactions & open quantums) |
       +---------------------------+---------------------------+
                                   |
                      Emits validated streams
                                   v
       +-------------------------------------------------------+
       |                 Isolated Output Dir                   |
       |   session_recovered.sndl + session_recovered.sndl.dma |
       |   (Marked: SndlV2CompletionStatus::Recovered)         |
       +---------------------------+---------------------------+
                                   |
                                   v
       +-------------------------------------------------------+
       |                 SmpReplayCoordinator                  |
       | - Strict dual capability gate                         |
       | - Restores from base snapshot                         |
       | - SmpReplayCheckpoint: Mid-stream state serialization  |
       | - Exact cluster parity: 0xc2bb6527cebf255c           |
       +-------------------------------------------------------+
```

---

## 2. Key Components

### 2.1 SndlV2Recorder Lifecycle State Machine
Every `.sndl` file contains an explicit recording status field in its header and trailer. When opened, it transitions immediately from `HeaderOnly` to `RecordingActive`. Upon orderly shutdown via `stop_recording()`, the final block is flushed, file size updated, and trailer marked `Completed`. If interrupted by process termination, power loss, or unhandled exception, the stream remains marked `RecordingActive`, signaling to all consumers that recovery scanning is mandatory before replay can be attempted.

### 2.2 DmaPayloadStore v2 Companion Architecture
The `.dma` companion file uses the 64-bit magic `0x56444D4144415441ull` (`VDMADATA`), version `2`, and stores:
1. `session_id`: 64-bit UUID uniquely bound to the active SNDL recorder.
2. `base_snapshot_hash`: 64-bit initial cluster state hash.
3. Indexed payload records: each tagged with global retirement sequence number, DMA channel, descriptor ID, payload length, and CRC32C checksum.
4. Trailing index table: written during `finalize()` with `final_state` flags (`Complete` or `RecoveredIncomplete`).

### 2.3 RecordingRecoveryScanner
The recovery engine performs an isolated, non-destructive salvage pass:
- Scans `input_sndl_path` and `input_dma_path` simultaneously.
- Verifies header magic, session ID equivalence, and base snapshot hash matching.
- Performs forward validation of SNDL blocks and DMA payload records.
- Identifies the last complete DMA transaction (every `DmaTransferBegin` has a corresponding `DmaTransferEnd`).
- Identifies the last closed quantum (every `CoreSelected` or `QuantumStart` has a matching `QuantumEnd`).
- Drops any trailing bytes or corrupted tail records.
- Copies clean records and matching payloads to the specified `output_directory`.
- Writes finalized headers and indexes marked `Recovered`.

### 2.4 SmpReplayCheckpoint & Fast-Forward Resumption
During replay of extensive multi-core executions, `SmpReplayCheckpoint` captures:
- Full cluster memory snapshot (`SmpSnapshotInspection`).
- Active scheduler quantum number and core index.
- Per-core retirement counters.
- Global retirement sequence.
- Sndl reader stream byte offset and event index.
- Pending virtual device states.

Replay can resume directly from any validated checkpoint, verifying that resuming produces identical final cluster hashes to continuous replay.
