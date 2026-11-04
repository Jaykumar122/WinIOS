# Architectural Timer and Counter Event Capture

## 1. Virtual Time vs External Counter Observations
WinIOS maintains a deterministic virtual time model (`VirtualTime`) driven by instruction retirement steps.
However, during execution, software may observe architectural counters whose instantaneous values can diverge from virtual progression when driven by external host pacing or real-world synchronization.

SNDL v2 provides explicit records for architectural counter reads:

| Event Type | Value | Boundary | Description |
|---|---|---|---|
| `CntpctRead` | 40 | `DuringInstruction` | Physical counter read (`CNTPCT_EL0`), `payload1 = counter_value`. |
| `CntvctRead` | 41 | `DuringInstruction` | Virtual counter read (`CNTVCT_EL0`), `payload1 = counter_value`. |
| `PmccntrRead` | 42 | `DuringInstruction` | PMU cycle counter read (`PMCCNTR_EL0`), `payload1 = cycle_count`. |
| `TimerComparatorExpired` | 43 | `AsynchronousInput` | Asynchronous comparator match signal injected (`payload1 = timer_id`, `payload2 = value`). |

---

## 2. Replay Contract
By capturing the exact 64-bit value returned by counter read instructions, future replay engines will not need to synthesize or approximate time. The recorded value will be directly returned to the destination register at the exact `pc` and `sub_instruction_ordinal`.
