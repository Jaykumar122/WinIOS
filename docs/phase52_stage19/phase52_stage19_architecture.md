# WinIOS Phase 52, Stage 19 — Replay Architecture Design

## 1. Executive Summary

The Deterministic Replay Engine provides cycle-accurate, bit-for-bit reconstructive replay of previously recorded WinIOS executions. It synthesizes three distinct subsystems developed across Phase 52:

1. **Architectural State Baseline (`.wsnap`)**: Established in Stage 17, providing an atomic, validated snapshot of CPU register files, SIMD/FP state, system registers, guest RAM, virtual timers, and GICv3 interrupt distributor/redistributor configurations.
2. **Non-Deterministic Log (`.sndl`)**: Established in Stage 18, providing a chronological, sequence-numbered log of all asynchronous events and external boundary inputs (MMIO reads, timer expirations, external interrupt deliveries, counter reads, and entropy).
3. **Deterministic Stepping Scheduler (`ReplayScheduler`)**: A cycle-accurate single-core instruction stepping engine that injects logged inputs at exact instruction retirement and sub-instruction boundaries without executing live host hardware queries.

```
       +-----------------------+      +-----------------------+
       |   Baseline Snapshot   |      |  Non-Deterministic   |
       |       (.wsnap)        |      |      Log (.sndl)      |
       +-----------+-----------+      +-----------+-----------+
                   |                              |
                   v                              v
       +-----------------------+      +-----------------------+
       |   Snapshot Restore    |      |  ReplayEventCursor    |
       |      (Stage 17)       |      | (Streaming Block Rdr) |
       +-----------+-----------+      +-----------+-----------+
                   |                              |
                   +--------------+---------------+
                                  |
                                  v
                   +------------------------------+
                   |      ReplaySession           |
                   |  (Isolated CPU, Memory, GIC) |
                   +--------------+---------------+
                                  |
                                  v
                   +------------------------------+
                   |     ReplayScheduler          |
                   |  - Instruction Stepping Loop |
                   |  - Asynchronous Injection    |
                   |  - ReplayInputRouter Hook    |
                   +--------------+---------------+
                                  |
                     +------------+------------+
                     |                         |
                     v                         v
       +-----------------------+ +-----------------------+
       | ReplayCheckpointMgr   | | Divergence Engine     |
       | Periodic State Hashes | | - First-divergence    |
       | & Memory Verification | | - 16 Categories       |
       |                       | | - divergence_report   |
       +-----------------------+ +-----------------------+
```

---

## 2. Core Subsystems and Component Invariants

### 2.1 ReplaySession

`ReplaySession` is the top-level coordinator managing the lifecycle of a replay execution:
- **Isolation**: Each replay session instantiates completely independent, isolated `CPU`, `Memory`, `VirtualTime`, and `GICv3` instances, ensuring zero interference with existing host state or parallel emulator runs.
- **Transactional Baseline Restore**: Baseline restoration occurs inside a transactional boundary. Before any guest execution commences, the `.wsnap` snapshot integrity is verified through CRC32C and schema validation.
- **Header Association Verification**: Verifies that the `.sndl` header's `base_snapshot_hash` and `snapshot_id` match the loaded snapshot baseline.

### 2.2 ReplayEventCursor

`ReplayEventCursor` is a sequential streaming reader for binary `.sndl` files:
- **Block-by-Block Buffered Streaming**: Streams records from fixed 48-byte block headers with block-level CRC32C verification, minimizing memory consumption while supporting multi-gigabyte logs.
- **Pending Asynchronous Event Matching**: Exposes `has_pending_asynchronous_input(core_id, retirement_count)` which inspects whether the current head of the stream represents an asynchronous arrival (e.g. interrupt or timer expiration) targeted at the current instruction retirement boundary.

### 2.3 ReplayInputRouter

`ReplayInputRouter` routes logged values into architectural query points:
- **Global Active Registration**: Registers via `set_active_replay_router` so CPU and MMIO accessors can query it inline with negligible branch overhead (`is_replay_active()`).
- **Synchronous vs. Asynchronous Interception**:
  - *Synchronous*: MMIO reads in `MemoryAccessor::read()`, counter reads (`CNTPCT_EL0`, `CNTVCT_EL0`), and entropy requests. Returns recorded value directly without executing live peripheral emulation.
  - *Asynchronous*: Intercepts interrupt delivery and timer expirations, verifying delivery order against the CPU's current retirement count.
- **Divergence Detection**: Validates access parameters (paddr, access width, core ID, retirement count, sub-instruction ordinal). If any mismatch is encountered, the router records detailed diagnostics into `ReplayDivergenceInfo` and triggers an immediate halt.

### 2.4 ReplayScheduler

`ReplayScheduler` implements the single-core instruction execution loop:
1. Prior to each instruction step, checks for pending asynchronous inputs matching `(core_id, retirement_count)` and delivers them into the CPU exception architecture.
2. Steps the CPU by one instruction via `cpu.step(memory)`.
3. Verifies no synchronous divergence occurred during the step.
4. Updates instruction retirement counters and triggers periodic checkpoints via `ReplayCheckpointManager`.
5. Halts when all logged events are consumed and target instruction limits are satisfied, or upon CPU halt.

---

## 3. Separation of Concerns

| Subsystem | Responsibilities | Modifies State? |
|---|---|---|
| `ReplayEngine` | Public facade API, CLI entrypoints, inspection | No |
| `ReplaySession` | Resource allocation, validation, lifecycle management | Yes (Session instance) |
| `ReplayEventCursor` | Sequential decoding of `.sndl` file blocks | No |
| `ReplayInputRouter` | Input parameter checking, logged value substitution | No (State queries only) |
| `ReplayScheduler` | Instruction stepping, asynchronous event dispatch | Yes (Drives CPU/Mem) |
| `ReplayStateHash` | Canonical hashing of registers, RAM, timers, GIC | No (Read-only) |
| `ReplayCheckpointManager` | Periodic state capture and drift prevention | No |
| `ReplayReport` | JSON exporting (`divergence_report.json`), summary formatting | No |
