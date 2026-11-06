# Eight-Core Scheduler Architecture
## Deterministic Single-Host-Thread Round-Robin Scheduling Across Cores 0..7

---

## 1. Scheduler Principles

The WinIOS `SmpScheduler` executes multi-core workloads entirely within a single host thread to guarantee complete execution determinism:

1. **Deterministic Selection**:
   - Round-robin traversal over registered core slots `0..core_count - 1`.
   - Cores in `Running` state are selected in strict ascending cyclic order.
   - Halted or sleeping cores are skipped deterministically without advancing retirement count.

2. **Quantum Stepping**:
   - An active core executes for at most `instruction_quantum` instructions per slice.
   - If an active core yields (e.g. WFI/WFE or breakpoint), the slice terminates early deterministically.

3. **Global Retirement Monotonicity**:
   - `global_retirement_sequence` increments by 1 for each retired instruction across all cores.
   - `R_global(t+1) = R_global(t) + delta_retired`.
   - Global retirement sequence is strictly monotonic.

4. **Multi-Core State Hash**:
   - `compute_canonical_multicore_hash()` combines:
     - PC, SP, CPSR, and general registers X0..X30 of all active cores 0..7.
     - Lifecycle states (Running, Halted, Suspended) of each core.
     - Interrupt pending states and exclusive reservation records.
     - SHA-256 / 64-bit folded cluster state hash.
