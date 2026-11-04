# Replay Verification and Multi-Run Determinism

## 1. Objective

The primary objective of multi-core deterministic replay is guaranteeing bit-identical architectural state across repeated replays of identical workloads, given a baseline `.wsnap v2` snapshot and recorded `.sndl v2` stream.

---

## 2. Verification Protocol

The verification protocol executes the following sequence:

1. **Synthetic Recording Phase**:
   - Initialize 2-core SMP cluster with 1000 instructions per quantum.
   - Core 0 executes compute and memory writes; Core 1 synchronizes and executes MMIO reads.
   - Generate initial `.wsnap v2` baseline snapshot with canonical cluster hash $H_0$.
   - Record 20 scheduling rounds with external interrupts, MMIO read events, and core yield events into `recording.sndl`.
   - Calculate and record ground-truth final cluster hash $H_{final}$.

2. **Multi-Run Replay Execution**:
   - Perform 5 consecutive isolated replay executions using `SmpReplayCoordinator`.
   - Before each replay, fresh state allocations are instantiated.
   - Snapshot is loaded, verifying initial hash matches $H_0$.
   - Replay engine advances through all recorded events and quantum boundaries until complete.
   - Canonical cluster state hash $H_{rep}^{(i)}$ is computed at the end of each run ($i \in \{1, 2, 3, 4, 5\}$).

3. **Invariance Assertion**:
   $$H_{rep}^{(1)} = H_{rep}^{(2)} = H_{rep}^{(3)} = H_{rep}^{(4)} = H_{rep}^{(5)} = H_{final}$$

---

## 3. Verification Results

In test group `Group H` (`Repeated Replay Invariance`):

| Replay Run | Initial State Hash Validated | Total Events Consumed | Instructions Retired | Final Cluster State Hash | Match Status |
|:---:|:---:|:---:|:---:|:---:|:---:|
| Run 1 | Yes ($H_0$) | 43 | 20,000 | `0x9e12f48a1b5c3301` | PASS |
| Run 2 | Yes ($H_0$) | 43 | 20,000 | `0x9e12f48a1b5c3301` | PASS |
| Run 3 | Yes ($H_0$) | 43 | 20,000 | `0x9e12f48a1b5c3301` | PASS |
| Run 4 | Yes ($H_0$) | 43 | 20,000 | `0x9e12f48a1b5c3301` | PASS |
| Run 5 | Yes ($H_0$) | 43 | 20,000 | `0x9e12f48a1b5c3301` | PASS |

### Consistency Metrics
- **Final Hash Divergence**: 0 bits.
- **Instruction Retirement Divergence**: 0 instructions across all cores.
- **Scheduler Decision Variance**: 0 decisions diverged.
- **Event Cursor Offset Errors**: 0 errors.

---

## 4. Adversarial and Mutation Verification

In test group `Group G` (`Replay Divergence Detection`):

1. **Core Divergence Mutation**:
   - Recorded scheduler chose Core 0; replay simulated Core 1 being chosen.
   - Engine detected divergence immediately at event sequence 1.
   - Engine halted execution and entered `SmpReplayState::Diverged`.
   - Error reported: `SchedulerCoreDivergence`.

2. **Quantum Bound Divergence Mutation**:
   - Recorded quantum instruction count was 1000; replay quantum terminated early at 500.
   - Engine caught early quantum boundary divergence.
   - Error reported: `QuantumMismatch`.

3. **External Event Corruption**:
   - Recorded MMIO read value was `0xCAFEBABE`; replay injected value `0xDEADBEEF`.
   - Core state diverged upon value inspection.
   - Error reported: `ExternalEventMismatch`.

4. **Retirement Sequence Out-of-Order Injection**:
   - Event injected at retirement count $R=5000$ when core had retired $R=4999$.
   - Engine asserted synchronization mismatch and rejected out-of-order retirement.
   - Error reported: `RetirementCountMismatch`.
