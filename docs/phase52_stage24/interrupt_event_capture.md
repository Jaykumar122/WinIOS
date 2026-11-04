# Interrupt & Exception Event Capture

## 1. Architectural Distinction: Nondeterministic Inputs vs Deterministic Consequences
In multi-core AArch64 systems, interrupts involve multiple stages:
1. **Asynchronous Injection / Assertion**: Nondeterministic external arrival (hardware lines, host timers).
2. **GIC Routing**: Deterministic routing based on distributor and redistributor configuration registers.
3. **Queue Insertion**: Deterministic placement into target core's pending FIFO queue.
4. **Interrupt Acceptance**: Deterministic evaluation against CPU priority mask and PSTATE interrupt masks (DAIF).
5. **Architectural Exception Entry**: Synchronous vector branch to exception vector table.
6. **Exception Return**: Execution of ERET instruction.

SNDL v2 records external non-deterministic arrivals as input events while capturing routing and delivery boundaries so future replay engines can verify state transitions:

---

## 2. Event Registry
| Event Type | Value | Boundary | Description |
|---|---|---|---|
| `ExternalInterruptAsserted` | 30 | `AsynchronousInput` | Hardware or host interrupt asserted (`payload1 = intid`, `source_id = device_id`). |
| `InterruptRouted` | 31 | `BetweenInstructions` | GIC router dispatched interrupt (`payload1 = intid`, `payload2 = from_core`). |
| `InterruptAccepted` | 32 | `ExceptionBoundary` | Core accepted interrupt delivery (`payload1 = intid`, `payload2 = exception_level`). |
| `ExceptionTaken` | 33 | `ExceptionBoundary` | Synchronous or asynchronous exception vector entry (`payload1 = ESR_ELx`, `payload2 = target_EL`, `pc = vector_addr`). |
| `ExceptionReturned` | 34 | `ExceptionBoundary` | ERET instruction completed (`payload1 = target_EL`, `pc = return_pc`). |

---

## 3. Preservation of FIFO Ordering
The `SmpInterruptRouter` enforces strict FIFO ordering for per-core interrupt delivery queues. When recorded in SNDL v2, `global_event_sequence` guarantees that multiple interrupts queued across different cores retain their exact temporal ordering.
