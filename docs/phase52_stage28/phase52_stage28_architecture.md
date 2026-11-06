# Phase 52, Stage 28 — Architecture Specification
## Eight-Core SMP Architecture Generalization, Scalability Stress Testing & Deterministic Replay Readiness

---

## 1. Architectural Scope & Objectives

Phase 52, Stage 28 generalizes the WinIOS multiprocessor architecture to fully support **8 virtual cores** across:
1. Architectural Core State (`SmpCoreState`, `SmpTopology`)
2. Deterministic Single-Host-Thread Scheduler (`SmpScheduler`)
3. Coherent Shared Memory (`SmpSharedMemory`)
4. Exclusive Monitor Subsystem (`SmpExclusiveMonitorManager`)
5. Cross-Core Interrupt Router (`SmpInterruptRouter`)
6. Virtualized Hardware Timers (`VirtualTime`)
7. Cluster Snapshotting (`SmpSnapshotSerializer`, `SmpSnapshotRestorer` via `.wsnap v2`)
8. Event Stream Format (`WSndlV2Header`, `WSndlV2Record` via `.sndl v2`)

Crucially, **Stage 28 strictly maintains the replay capability gate rejecting eight-core replay** (`ReplayErrorCode::UnsupportedConfiguration`). Replay continues to be fully supported for 1-core, 2-core, and 4-core execution with zero regressions.

```
       +-------------------------------------------------------------+
       |                     SmpTopology                             |
       |  - Valid Topologies: 1, 2, 4, 8 Cores (Power-of-Two)       |
       |  - Core Limit: MAX_CORES = 8                                |
       |  - Capability Gate: Replay limited to <= 4 cores             |
       +------------------------------+------------------------------+
                                      |
         +----------------------------+----------------------------+
         |                                                         |
         v                                                         v
+------------------+                                      +------------------+
|  .wsnap v2 File  |                                      |  .sndl v2 File   |
| (8-Core Capable) |                                      | (8-Core Stream)  |
+--------+---------+                                      +--------+---------+
         |                                                         |
         v                                                         v
+------------------------+                                +------------------------+
|  SmpSnapshotRestorer   |                                |   SndlV2ReplayReader   |
|  - 8-Core RAM restore  |                                |   - Bounded streaming  |
|  - Per-core registers  |                                |   - CRC32C verify      |
|  - Canonical hash H0   |                                +-----------+------------+
+------------------------+                                            |
                                                                      v
                                                          +------------------------+
                                                          |  SmpReplayCoordinator  |
                                                          |  - Gate: cores <= 4 OK  |
                                                          |  - Gate: cores > 4 ERR  |
                                                          +------------------------+
```

---

## 2. Component Generalization Details

### 2.1 SmpTopology
- Introduced `SmpTopology` class in `src/cpu/smp_topology.h`.
- Strictly enforces valid core counts: 1, 2, 4, 8 cores.
- Rejects non-power-of-two (e.g. 3, 5, 6, 7) and counts exceeding 8 (e.g. 16, 32).
- Validates scheduler quantum (1..1,000,000 instructions) and shared RAM bounds.

### 2.2 Global Scheduler (SmpScheduler)
- Generalizes round-robin scheduling for 8 active core slots.
- Global retirement counter increments strictly monotonically across core switches.
- Per-core retirement counters track execution independently up to `UINT64_MAX`.
- Canonical multicore hash computation incorporates states across all 8 cores.

### 2.3 Shared Memory & Exclusive Monitors
- Shared physical memory handles atomic transactions and interleaved accesses across 8 cores.
- Exclusive monitor manager tracks reservation granules (64 bytes) for 8 core IDs.
- Core-targeted cache clears, STXR/LDXR semantics operate with deterministic isolation.

### 2.4 Interrupt Routing & Virtual Timers
- Inter-processor interrupts (SGIs) and peripheral interrupts (SPIs/PPIs) route to any core 0..7.
- Virtual timer events fire based on monotonic `VirtualTime` counter.

### 2.5 Replay Capability Gate
- `SmpReplayCoordinator::prepare()` validates core count:
  - Cores 1, 2, 4: Supported and verified deterministic.
  - Cores 8: Strictly rejected with `ReplayErrorCode::UnsupportedConfiguration`.
- `allow_uncaptured_dma`: Strictly set to false, maintaining rejection of asynchronous DMA (`BLK-SMP-03`).
