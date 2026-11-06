# Phase 52, Stage 28 — Architectural Architecture Specification
## Eight-Core Multi-Processor Foundation, Subsystem Scaling & Gated Deterministic Replay

---

## 1. Top-Level Architecture

The WinIOS Stage 28 architecture scales virtual multi-processing from 4 cores to 8 cores using an isolated single-host-thread deterministic execution loop:

```
+---------------------------------------------------------------------------------------+
|                                     SmpTopology                                       |
|  - Permitted Core Topologies: 1, 2, 4, 8                                              |
|  - Dynamic Range Bounds: 1 <= quantum <= 1,000,000                                    |
|  - Memory Alignment: 4KB Page Aligned Physical Guest Memory                           |
|  - Replay Capability Policy: Core counts > 4 strictly rejected                        |
|  - DMA Capability Policy: Asynchronous DMA strictly rejected (BLK-SMP-03 guard)       |
+---------------------------------------------------------------------------------------+
                                           |
         +---------------------------------+---------------------------------+
         |                                                                   |
         v                                                                   v
+------------------------------------+             +------------------------------------+
|            SmpScheduler            |             |           SmpSharedMemory          |
| - 8 Core Slots (Cores 0..7)        |             | - 64MB Backing Guest Physical RAM  |
| - Deterministic Round-Robin        |             | - Core-tagged Memory Operations    |
| - Monotonic R_global Increment     |             | - Deterministic Quantum Slices     |
| - Independent Per-Core R_core[i]   |             +-----------------+------------------+
+-----------------+------------------+                               |
                  |                                                  |
                  +------------------------+-------------------------+
                                           |
         +---------------------------------+---------------------------------+
         |                                                                   |
         v                                                                   v
+------------------------------------+             +------------------------------------+
|    SmpExclusiveMonitorManager      |             |         SmpInterruptRouter         |
| - 64-byte Reservation Granules     |             | - Dedicated Per-Core Queues 0..7   |
| - Local Monitor per Core (0..7)    |             | - PPI / SPI / SGI Inter-Processor  |
| - Cross-Core Invalidation on Store |             | - Zero Cross-Queue Leakage         |
+------------------------------------+             +------------------------------------+
                                           |
         +---------------------------------+---------------------------------+
         |                                                                   |
         v                                                                   v
+------------------------------------+             +------------------------------------+
|            VirtualTime             |             |        SmpSnapshotSerializer       |
| - Physical Counter CNTPCT_EL0      |             | - .wsnap v2 Header core_count = 8  |
| - Guest-Visible Isolation          |             | - 8 Per-Core State Sections        |
| - Independent Per-Core Comparators |             | - Sub-millisecond Save/Restore     |
+------------------------------------+             +------------------------------------+
```

---

## 2. Capability Gating Architecture

Deterministic replay across the emulator is governed by the capability matrix:

| Core Configuration | Replay Support | Enforcement Mechanism | Error Code |
|---|---|---|---|
| **1 Core** | Fully Supported | Single-core replay pipeline | `ReplayErrorCode::Success` |
| **2 Cores** | Fully Supported | `SmpReplayCoordinator` 2-core loop | `ReplayErrorCode::Success` |
| **4 Cores** | Fully Supported | `SmpReplayCoordinator` 4-core loop | `ReplayErrorCode::Success` |
| **8 Cores** | Strictly Gated | Capability Check in `prepare()` | `ReplayErrorCode::UnsupportedConfiguration` |
| **>8 Cores / Odd** | Rejected | Topology validation in `SmpTopology` | `TopologyError::InvalidCoreCount` |
| **Uncaptured DMA** | Rejected | DMA capability gate | `ReplayErrorCode::UnsupportedConfiguration` |

This multi-tiered gating ensures that architectural scaling can be thoroughly benchmarked, stressed, and validated while keeping replay guarantees provably unbroken.
