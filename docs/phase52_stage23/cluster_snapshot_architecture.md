# Cluster Snapshot Architecture

## 1. Architectural Philosophy

The WinIOS Multi-Core Cluster Snapshot subsystem captures and restores the complete multicore execution environment developed in Phase 52 Stage 22 (`SmpCoreState`, `SmpSharedMemory`, `SmpScheduler`, `SmpExclusiveMonitorManager`, `SmpInterruptRouter`).

Key architectural principles:
1. **Single Logical Instant**: Every cluster snapshot represents a consistent logical boundary across all cores.
2. **Single Host Thread Quiescence**: Exploits single-host-thread cooperative execution to ensure that no virtual core is in the middle of executing an instruction when the snapshot is taken.
3. **Single Memory Serialization**: Shared physical memory is serialized exactly once, avoiding $N \times$ memory bloat.
4. **Relational Fidelity**: Preserves cross-core architectural relationships:
   - Exclusive monitor reservations.
   - Pending cross-core interrupts in FIFO queues.
   - Global sequence and round-robin scheduler position.
   - Banked architectural state per core.
5. **Transactional Atomicity**: Complete validation before any mutation of the live cluster during restoration, and atomic file commit during capture.

---

## 2. Cluster Component Map

```mermaid
graph TD
    subgraph WSnapV2["Multi-Core Snapshot (.wsnap v2)"]
        H["WSnapHeader (version=2, core_count=N)"]
        D["Section Directory Table"]
        M["METADATA (0x07)"]
        CD["CORE_DIRECTORY (0x11)"]
        CAS["CORE_ARCHITECTURAL_STATE (0x12) [xN]"]
        SM["SHARED_MEMORY (0x13) [Sparse 4KB]"]
        SCH["SCHEDULER (0x14)"]
        EM["EXCLUSIVE_MONITOR (0x15)"]
        IR["INTERRUPT_ROUTER (0x16)"]
        INT["INTEGRITY (0x17) [Canonical Multicore Hash]"]
    end

    subgraph ClusterState["Live Cluster Environment"]
        LiveSched["SmpScheduler (Quantum Q, Seq, Core Index)"]
        LiveMem["SmpSharedMemory (Single RAM)"]
        LiveMon["SmpExclusiveMonitorManager"]
        LiveRouter["SmpInterruptRouter"]
        Core0["CPU 0 / SmpCoreState 0"]
        CoreN["CPU N / SmpCoreState N"]
    end

    CAS --> Core0
    CAS --> CoreN
    SM --> LiveMem
    SCH --> LiveSched
    EM --> LiveMon
    IR --> LiveRouter
```

---

## 3. Snapshot Lifecycles

### Capture Lifecycle
`Idle -> Quiescing -> Capturing -> Validating -> Finalizing -> Completed`
- Captures states of all components into memory buffers.
- Verifies CRC32C of all sections and calculates canonical multicore hash.
- Writes to temporary file `<output>.tmp`, verifies disk write, and performs atomic rename.

### Restore Lifecycle
`Validating -> Staging -> Cross-Verification -> Committing -> Active`
- Validates file format, directory bounds, and payload CRC32C without touching target.
- Deserializes into staging data structures.
- Verifies compatibility with target scheduler (core count, memory size).
- Atomically commits state to shared memory, virtual CPUs, monitor, interrupt router, and scheduler.
