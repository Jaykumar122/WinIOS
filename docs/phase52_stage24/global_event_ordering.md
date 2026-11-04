# Global Event Ordering Architecture

## 1. Multi-Core Ordering Model
In a multi-core execution environment, events occur across different virtual CPU cores, scheduling boundaries, and asynchronous peripheral sources. To establish a sound foundation for future deterministic replay, SNDL v2 defines three distinct, complementary ordering sequences:

1. **Global Event Sequence (`global_event_sequence`)**:
   - Strictly monotonic integer ($S_0 < S_1 < S_2 < \dots$).
   - Assigned by `SndlV2Recorder` upon receiving or generating an event.
   - Represents the canonical FIFO observation order of all recorded inputs and decisions under the single-host-thread scheduler.
   - Duplicate or decreasing sequence numbers are strictly rejected during validation.

2. **Global Instruction Retirement Sequence (`global_retirement_sequence`)**:
   - Monotonic integer tracking cumulative guest instructions retired across all cores in the cluster.
   - Progresses by 1 for each instruction stepped by any core.
   - Non-decreasing throughout execution ($R_{i} \ge R_{i-1}$).

3. **Per-Core Instruction Retirement Count (`core_retirement_count`)**:
   - Monotonic per-core counter tracking instructions retired by `core_id`.
   - Never regresses ($C_{core, i} \ge C_{core, i-1}$).
   - Establishes the exact architectural point at which an asynchronous or external event took effect relative to that core's execution stream.

---

## 2. Event Boundary Types
Every `SndlV2Record` is tagged with an architectural boundary type (`SndlV2BoundaryType`):
- `BeforeInstruction (1)`: Event takes effect before the instruction at `pc` is decoded or executed.
- `DuringInstruction (2)`: Event represents a synchronous value returned during instruction execution (e.g., MMIO read, counter read, load/store exclusive).
- `AfterInstruction (3)`: Event generated immediately following instruction retirement.
- `BetweenInstructions (4)`: Boundary between instructions where asynchronous state changes can be safely sequenced.
- `AsynchronousInput (5)`: External or peripheral arrival (e.g., hardware interrupt, host device event).
- `ExceptionBoundary (6)`: Architectural exception entry or return boundary.
- `SchedulerBoundary (7)`: Quantum slice boundary (quantum start, quantum end, early truncation, core selection).
- `BarrierBoundary (8)`: Memory barrier synchronization or multi-core coordination barrier.
- `CoreLifecycleBoundary (9)`: Core lifecycle state transition (start, halt, WFI, WFE, resume).

---

## 3. Sub-Instruction Ordinal
For instructions that produce multiple non-deterministic observations or compound effects within a single retirement step, the `sub_instruction_ordinal` field (u16) provides secondary deterministic tie-breaking ($0, 1, 2\dots$).

---

## 4. Sequence Validation Invariants
The `SndlV2Validator` rigorously verifies the following invariant properties:
$$\forall i > 0: S_i > S_{i-1}$$
$$\forall i > 0: R_i \ge R_{i-1}$$
$$\forall i > 0: C_{\text{core}(i), i} \ge C_{\text{core}(i), \text{prev}(i)}$$
$$\text{core\_id} < \text{header.core\_count}$$
Any violation results in immediate rejection with `SndlV2ErrorCode::InvalidSequence`.
