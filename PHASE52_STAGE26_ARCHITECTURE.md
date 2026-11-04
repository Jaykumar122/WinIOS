# WinIOS Phase 52, Stage 26 — Architecture Specification
## Multi-Core Deterministic Replay Engine: Architecture, Event Consumption & Scheduler Coordination

---

## 1. System Overview

The Multi-Core Deterministic Replay Engine reconstructs identical multicore execution paths by synchronizing:
1. Architectural baseline cluster state loaded from `.wsnap v2`.
2. Sequential recorded events streamed from `.sndl v2`.
3. Discrete scheduler decisions (`CoreSelected`, `QuantumStart`, `QuantumEnd`, `EarlyTruncation`).
4. Monotonic retirement synchronization ($R_{global}$, $R_{core}$) across all cores.

```
       +-------------------------------------------------------------+
       |                  SmpReplayCoordinator                       |
       |  - Replay State Machine (Uninit->Prepared->Running->Done)   |
       |  - 2-Core Capability Gate (Enforces 2-Core & Blocker rules) |
       +------------------------------+------------------------------+
                                      |
         +----------------------------+----------------------------+
         |                                                         |
         v                                                         v
+------------------+                                      +------------------+
|  .wsnap v2 File  |                                      |  .sndl v2 File   |
| (Cluster State)  |                                      | (Event Stream)   |
+--------+---------+                                      +--------+---------+
         |                                                         |
         v                                                         v
+------------------------+                                +------------------------+
|  SmpSnapshotRestorer   |                                |   SndlV2ReplayReader   |
|  - Shared RAM restore  |                                |   - Chunk parsing      |
|  - Per-core register   |                                |   - CRC32C verify      |
|  - Canonical hash H0   |                                +-----------+------------+
+------------------------+                                            |
                                                                      v
                                                          +------------------------+
                                                          |  SmpReplayEventCursor  |
                                                          |  - Monotonic sequence  |
                                                          |  - Lookahead peek      |
                                                          |  - Chunk index cache   |
                                                          +-----------+------------+
                                                                      |
+---------------------------------------------------------------------+
|
v
+--------------------------------------------------------------------------+
| Scheduler & Core Execution Replay Loop                                   |
|                                                                          |
| 1. Scheduler re-evaluates next core -> Validate matches recorded core.   |
| 2. Advance quantum -> Validate recorded quantum bound.                   |
| 3. Step core instructions -> Track R_core and R_global retirements.      |
| 4. At event retirement threshold -> Deliver MmioRead / Interrupt.        |
| 5. On quantum completion / early yield -> Validate QuantumEnd match.     |
| 6. On divergence -> Halt immediately and log comprehensive diagnostics.  |
+--------------------------------------------------------------------------+
```

---

## 2. Key Components

### 2.1 Replay State Machine (`SmpReplayState`)
Defined in `src/replay/smp_replay_types.h`:
- `Uninitialized`: Coordinator instance created, no snapshot or log loaded.
- `Prepared`: Snapshot validated, SNDL header and first chunk verified, capability gate passed.
- `Running`: Actively stepping scheduler quantums and advancing event cursor.
- `Completed`: All events consumed, execution reached recorded terminal boundary.
- `Diverged`: Divergence detected between recorded log and replay execution. Execution halted.

### 2.2 Event Cursor (`SmpReplayEventCursor`)
Defined in `src/replay/smp_replay_cursor.h`:
- Monotonic sequence validation: $S_{i+1} = S_i + 1$.
- Constant-time event retrieval from in-memory chunk buffer.
- Automatic chunk advancement via `SndlV2ReplayReader`.

### 2.3 Replay Coordinator (`SmpReplayCoordinator`)
Defined in `src/replay/smp_replay_coordinator.h`:
- Interfaces with `SmpScheduler` to drive round-robin quantums.
- Interfaces with `SmpInterruptRouter` to deliver recorded interrupts (`ExternalInterrupt`).
- Injects external MMIO data on demand.
- Computes final canonical cluster hash upon completion.

### 2.4 Capability Gate (`SmpReplayCapabilityConfig`)
Enforces strict execution scope:
- Only 2-core replay is allowed.
- `allow_experimental_smp = true` required.
- BLK-SMP-03 (`permit_host_dma = false`) and BLK-SMP-04 (`permit_host_wall_clock = false`) strictly enforced.

---

## 3. Divergence Handling and Diagnostics

Upon any divergence (`SchedulerCoreDivergence`, `QuantumMismatch`, `RetirementCountMismatch`, `ExternalEventMismatch`, `StateHashMismatch`):
1. State transitions immediately to `SmpReplayState::Diverged`.
2. Divergence details (`SmpReplayDivergence`) are populated:
   - Sequence number and event type.
   - Core ID.
   - Expected value vs Actual value.
   - Global retirement count and local core retirement count.
   - Error message.
3. Execution terminates without corrupting remaining logs or snapshots.
