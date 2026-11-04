# Four-Core Instruction Retirement Synchronization

## 1. Dual-Clock Retirement Model

In 4-core SMP execution, instructions retire across four separate cores. To maintain deterministic synchronization, the system implements a dual-clock tracking mechanism:

1. **Global Retirement Clock ($R_{global}$)**:
   - A single, monotonically increasing 64-bit integer tracking the total number of instructions retired across all four cores.
   - Initialized from snapshot state; advances by 1 on every successful non-faulting instruction execution.

2. **Per-Core Retirement Clocks ($R_{core}[0..3]$)**:
   - Separate 64-bit counters maintaining the instruction retirement count of each individual core.
   - Invariant: $\sum_{i=0}^3 R_{core}[i] = R_{global}$.

---

## 2. Faulting Instruction Semantics

A critical invariant for deterministic replay is that instructions triggering architectural synchronous exceptions (e.g. data aborts, undefined opcodes, MMU translation faults):
- **Do not retire**: Neither $R_{core}[i]$ nor $R_{global}$ is incremented.
- The quantum slice is immediately terminated with `QuantumTruncationReason::ExceptionRaised`.
- An `ExceptionTaken` event is matched in the SNDL v2 stream.
- Execution transfers to the vector table entry.

---

## 3. Boundary Alignment

Every external input event in SNDL v2 specifies:
$$(core\_id, R_{core}[core\_id])$$
This allows the replay coordinator to pause execution of core $core\_id$ exactly when its local retirement count matches the event timestamp, inject the external data, and resume stepping.
