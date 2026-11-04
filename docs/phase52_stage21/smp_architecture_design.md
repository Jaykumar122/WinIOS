# Multi-Core SMP Replay Architecture Design

## 1. Problem Statement: The Multi-Core Determinism Dilemma

In a single-core virtual machine, determinism requires only recording inputs that cross the single core boundary (MMIO, external interrupts, timers, and entropy). In an AArch64 Symmetric Multiprocessing (SMP) system, however:
1. Multiple cores execute concurrently against a shared physical memory space.
2. The order in which stores executed on Core $A$ become visible to loads on Core $B$ is nondeterministic under host OS thread scheduling.
3. Software Generated Interrupts (SGIs) send inter-processor interrupts (IPIs) across cores at nondeterministic cycle phases.
4. Per-core virtual time counters drift if host worker threads experience scheduling preemption or hyperthreading contention.

This document presents the architectural design required to achieve reproducible multi-core replay in future WinIOS phases.

---

## 2. Global Execution Ordering Without Host Thread Dependency

A deterministic SMP replay engine must decouple guest execution progress from the host operating system thread scheduler. It requires establishing a canonical **Global Retirement Timeline**:

```
Core 0:  ──[I0]──[I1]──[I2]─────────[I3 (Store X)]────────────────[I4]──►
                          \                 \
                           \                 \ Visibility Barrier
                            \                 ▼
Core 1:  ──────[I0]──────────[I1 (SGI)]───────[I2 (Load X)]───────[I3]──►
                               ▲
                        SGI Trigger Point
```

### Deterministic Coordinate System
Every guest action is indexed by a 3-tuple coordinate:
$$\text{Boundary Coordinate} = \langle \text{CoreID}, \text{RetiredInstructions}, \text{GlobalEpoch} \rangle$$
- `CoreID`: Unique core index ($0 \dots N-1$).
- `RetiredInstructions`: Monotonically increasing per-core instruction counter.
- `GlobalEpoch`: Monotonically increasing cluster-wide synchronization milestone counter incremented at every inter-core barrier, SGI delivery, or shared memory lock acquisition.

---

## 3. Required SNDL Schema Extensions (Future Version 2.0 Proposal)

The current `.sndl` (v1.0) format supports only single-core external inputs. Replaying SMP workloads requires extending the record schema with five inter-core event types:

| Event Type | Opcode | Fields & Payload | Replay Semantics |
|---|---:|---|---|
| `CoreLifecycleEvent` | `0x10` | `core_id`, `action` (START/STOP/SUSPEND/WAKE), `entry_pc`, `context_id` | Synchronously halts or boots a target secondary core at a specific instruction boundary. |
| `SgiDelivery` | `0x11` | `src_core`, `target_core`, `intid` (0–15), `target_instruction_boundary` | Forces GICv3 CPU interface of `target_core` to take an IRQ exception precisely before `target_instruction_boundary`. |
| `BarrierSyncPoint` | `0x12` | `initiator_core`, `barrier_type` (ISB/DSB/DMB), `participating_cores_mask` | Enforces a global synchronization barrier across all indicated cores. |
| `ExclusiveMonitorUpdate` | `0x13` | `core_id`, `physical_address`, `action` (TAG_SET / CLEAR_GLOBAL) | Emulates the ARMv8 global exclusive monitor deterministically across cores. |
| `MemoryVisibilityToken` | `0x14` | `writer_core`, `reader_core`, `cacheline_addr`, `writer_inst`, `reader_inst` | Enforces store-to-load ordering across cores during data race resolution. |

*Backward Compatibility Rule: SNDL v1.0 files (`core_count == 1`) remain 100% valid. SNDL v2.0 files will increment `WSndlHeader::version = 2` and require multi-core engine support.*

---

## 4. Multi-Core Snapshot Consistency

A multi-core snapshot (`.wsnap`) must capture a consistent cut of the entire cluster:
- **Per-Core CPU Sections**: Contains $N$ distinct CPU context sections (registers `X0`–`X30`, `PC`, `SP`, `PSTATE`, `V0`–`V31`, EL-specific system registers, and core run-states).
- **Global Memory Section**: A single consolidated physical RAM image.
- **Cluster GICv3 Section**: Single Distributor state + $N$ Redistributor states + $N$ CPU interface register states.
- **VirtualTime Section**: Baseline cluster ticks and per-core offset counters (`CNTVOFF_EL2`).

### Consistent Cut Invariant
All cores must be brought to an instruction retirement boundary simultaneously before snapshot serialization begins. No core may execute instructions while memory chunk hashing is in progress.

---

## 5. Session Preparation & Rejection Policy

In Phase 52 Stage 21, the replay engine enforces strict rejection of multi-core requests:
```cpp
// In ReplaySession::prepare():
if (session_config_.mode == ReplayMode::MultiCore || session_config_.core_count > 1) {
    if (!session_config_.allow_experimental_smp) {
        stats_.completion_status = ReplayCompletionStatus::UnsupportedConfiguration;
        return ReplayResult::fail(
            ReplayErrorCode::UnsupportedConfiguration,
            "Multi-core SMP replay is not supported in Phase 52 Stage 21"
        );
    }
}
```
Multi-core replay will remain gated until all prerequisites are fully implemented and verified.
