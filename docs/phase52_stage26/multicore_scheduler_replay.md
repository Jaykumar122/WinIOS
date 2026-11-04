# WinIOS Phase 52 Stage 26 — Multi-Core Scheduler Replay & Coordination

## 1. Scheduler Replay Architecture

In single-core replay, scheduling is trivial: one core executes linearly. In a multi-core environment, execution proceeds in discrete instruction slices (quanta) scheduled deterministically by `SmpScheduler`.

During recorded execution, `SmpScheduler` emits four primary types of scheduler events into SNDL v2:
1. `CoreSelected` (type 23): Records the core selected by round-robin scheduling for the slice.
2. `QuantumStart` (type 20): Records the quantum budget $Q$ allocated to the core.
3. `QuantumEnd` (type 21): Records the actual instructions retired in the quantum and the termination reason.
4. `EarlyTruncation` (type 22): Emitted when a quantum terminates before budget exhaustion (e.g. WFI, HLT, Exception).

```
Recorded SNDL v2 Stream:
  [Event 1] CoreSelected (Core 0)
  [Event 2] QuantumStart (Core 0, Budget=10)
     ... (Core 0 executes up to 10 instructions) ...
  [Event 3] QuantumEnd (Core 0, Executed=10, Reason=Exhausted)
  [Event 4] CoreSelected (Core 1)
  [Event 5] QuantumStart (Core 1, Budget=10)
     ... (Core 1 executes up to 10 instructions) ...
  [Event 6] QuantumEnd (Core 1, Executed=10, Reason=Exhausted)
```

---

## 2. Coordination and Verification Protocol

During replay, `SmpReplayCoordinator` drives execution by validating each scheduler event before and after execution:

### 2.1 Slice Initialization
1. Coordinator checks for any pending asynchronous external events (interrupts, timers) due before the slice.
2. Coordinator consumes `CoreSelected` from the event cursor and verifies `rec.core_id == expected_core_id`. If `rec.core_id` does not match, a `SchedulerMismatch` divergence is raised.
3. Coordinator consumes `QuantumStart` and verifies that the budget $Q$ matches the expected allocation.

### 2.2 Quantum Instruction Execution
The core executes up to $Q$ instructions on the target CPU using `slot.cpu->step(shared_memory->raw_memory())`.
- If an architectural synchronous exception occurs:
  - `slot.cpu->exception_taken_in_step()` is true.
  - Coordinator consumes the recorded `ExceptionTaken` event.
  - The faulting instruction does **not** retire.
  - The quantum is truncated with reason `QuantumTruncationReason::ExceptionRaised`.
- If the core halts (`HLT`), executes `WFI`, or executes `WFE`, the quantum terminates early with the respective reason.

### 2.3 Slice Finalization
1. Coordinator consumes `QuantumEnd` and verifies:
   - `rec.core_id == core_id`
   - `rec.payload1 == actual_instructions_executed` (Retirement Parity)
   - `rec.payload2 == actual_truncation_reason` (Quantum Boundary Parity)
2. If the slice ended early, coordinator consumes the companion `EarlyTruncation` event and verifies the truncation reason code.
3. If any field disagrees, execution stops immediately with a structured `SmpReplayDivergence`.
