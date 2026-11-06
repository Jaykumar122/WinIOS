# Eight-Core Scheduler Replay Coordination

## 1. Single-Host-Thread Deterministic Multicore Scheduling
WinIOS employs a single-host-thread scheduler (`SmpScheduler`) to eliminate host-thread non-determinism. Scheduling across 8 cores operates under strict quantum slices with instruction limits.

## 2. Replay Scheduler Coordination Loop
During replay, `SmpReplayCoordinator` drives the cluster by matching recorded scheduling decisions against current scheduler state:
1. `CoreSelected`: Asserts that the scheduler chooses the exact core recorded in `r.core_id`.
2. `QuantumStart`: Sets the quantum slice limit `r.payload1` (instruction quantum).
3. `step_quantum()`: Advances the active core until the instruction limit is reached, a blocking instruction (`WFI`/`WFE`) is hit, or an early truncation condition occurs.
4. `QuantumEnd`: Validates that instructions retired match recorded `r.payload1` and truncation flags match `r.payload2`.

## 3. Slice Boundary Alignment
If a core terminates early (e.g., due to an interrupt or MMIO boundary), an `EarlyTruncation` event records the exact instruction count and cause, ensuring that during replay the scheduler truncates at the exact same boundary.
