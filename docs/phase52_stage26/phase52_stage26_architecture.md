# WinIOS Phase 52 Stage 26 — Multi-Core Deterministic Replay Architecture

## 1. Architectural Mission & Overview

Phase 52 Stage 26 establishes the first operational **Multi-Core Deterministic Replay Engine** within the WinIOS execution environment. Following the successful delivery of SNDL v2 multicore event recording in Stage 24 and adversarial recording verification in Stage 25, Stage 26 implements the counterpart replay system capable of reconstructing multi-core guest execution in exact causal sequence, reproducing guest-visible architectural behavior, instruction retirements, and shared memory state without divergence.

Replay across multiple cores introduces profound concurrency challenges that do not exist in single-core replay:
1. **Instruction Quantum Boundaries:** The replay engine must coordinate scheduling decisions (`CoreSelected`, `QuantumStart`, `QuantumEnd`, `EarlyTruncation`) to ensure the virtual cores interleave deterministically in identical quantum budgets.
2. **Retirement Parity:** Instruction retirement accounting across global cluster sequences and per-core counters must align continuously. Faulting instructions that raise synchronous exceptions do not increment architectural retirement counters and must not corrupt replay state.
3. **Controlled Scope:** To guarantee zero uncaptured nondeterminism, Stage 26 implements a strict **Capability Gate**. Multicore replay is enabled exclusively for 2-core workloads whose nondeterministic inputs are proven complete and verified. Replay for 4-core and 8-core configurations remains explicitly rejected, and workloads depending on uncaptured host DMA (`BLK-SMP-03`) or host wall-clock reads (`BLK-SMP-04`) are halted immediately.

```
                    +---------------------------------------+
                    |        .wsnap v2 Snapshot File        |
                    +---------------------------------------+
                                        |
                                        v (Hash & Topology Validation)
+-------------------+      +-------------------------+      +--------------------+
|  .sndl v2 Stream  | ---> |   SndlV2ReplayReader    | ---> | SmpReplayEventCursor
+-------------------+      +-------------------------+      +--------------------+
                                                                      |
                                                                      v
                                                        +---------------------------+
                                                        |   SmpReplayCoordinator    |
                                                        +---------------------------+
                                                              /               \
                                                             /                 \
                                                            v                   v
                                                +----------------+    +-------------------+
                                                | SmpScheduler   |    | SmpSharedMemory   |
                                                | (2 Cores: Q)   |    | (Unified Coherent)|
                                                +----------------+    +-------------------+
```

---

## 2. Core Architectural Subsystems

The Stage 26 replay subsystem is organized into modular components with strict ownership boundaries:

### 2.1 `SndlV2ReplayReader`
- **Location:** `src/replay/smp_replay_reader.h`, `src/replay/smp_replay_reader.cpp`
- **Role:** Sequential, block-buffered stream reader for validated `.sndl v2` binary recordings.
- **Responsibilities:**
  - Validates 64-byte `WSndlV2Header` (magic, version == 2, CRC32C, memory capacity, baseline snapshot hash).
  - Validates 64-byte `WSndlV2Footer` (magic, completion status == `Completed`, zero dropped events, CRC32C).
  - Streams 48-byte `WSndlV2BlockHeader` blocks with dynamic payload buffering and CRC32C integrity checking.
  - Exposes 80-byte `SndlV2Record` instances in monotonic global sequence order.
  - Never mutates the underlying file on disk.

### 2.2 `SmpReplayEventCursor`
- **Location:** `src/replay/smp_replay_cursor.h`, `src/replay/smp_replay_cursor.cpp`
- **Role:** Active cursor tracking global sequence position and providing atomic validation semantics.
- **Responsibilities:**
  - Tracks `current_global_sequence`, `consumed_count`, and `last_record`.
  - Supports non-destructive inspection (`peek()` and `peek_type_is()`).
  - Implements atomic consumption (`consume_expected_type()` and `consume()`).
  - Enforces sequence continuity ($S_{i+1} = S_i + 1$) under strict monotonicity.
  - Sequence gaps or payload type mismatches prevent cursor advancement and report diagnostic failure.

### 2.3 `SmpReplayCoordinator`
- **Location:** `src/replay/smp_replay_coordinator.h`, `src/replay/smp_replay_coordinator.cpp`
- **Role:** High-level execution coordinator linking the event stream, `SmpScheduler`, shared memory, and virtual cores.
- **Responsibilities:**
  - Enforces the 2-core Capability Gate.
  - Validates and restores the initial baseline `.wsnap v2` cluster snapshot.
  - Verifies canonical cluster state hash before and after snapshot restoration.
  - Coordinates quantum stepping: verifies `CoreSelected` and `QuantumStart` events before CPU stepping.
  - Steps cores instruction-by-instruction, injects external events, and verifies `QuantumEnd` / `EarlyTruncation`.
  - Compares global and per-core retirement counters.
  - Stops immediately upon divergence and constructs structured diagnostic records.

### 2.4 `SmpReplayCoreState`
- **Location:** `src/replay/smp_replay_core_state.h`
- **Role:** Lightweight per-core replay telemetry tracking allocated quantums, early truncations, active budget, and lifecycle.

---

## 3. Strict Capability Gate & Safeguards

The Stage 26 capability gate permits multi-core deterministic replay **only** when all of the following conditions are met:
1. Snapshot file is `.wsnap v2` with valid directory, CRC32C checksums, and exactly 2 registered cores.
2. SNDL recording is `.sndl v2` with matching core count (2), matching memory capacity, and matching `base_snapshot_hash`.
3. SNDL recording possesses a valid clean footer with `completion_status == Completed` and `dropped_events == 0`.
4. Workload contains no uncaptured host DMA (`BLK-SMP-03`).
5. Workload contains no unvirtualized host wall-clock reads (`BLK-SMP-04`).

Any violation halts preparation immediately, sets state to `Unsupported` or `Diverged`, and returns `ReplayErrorCode::UnsupportedConfiguration`. Replay for 4-core and 8-core workloads remains explicitly disabled.
