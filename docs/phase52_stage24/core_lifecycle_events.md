# Core Lifecycle Event Capture

## 1. Lifecycle Event Model
Virtual CPU cores in a multi-core cluster transition between distinct operational states. SNDL v2 provides structured records for all supported architectural lifecycle transitions:

| Event Type | Value | Boundary | Description |
|---|---|---|---|
| `CoreCreated` | 10 | `CoreLifecycleBoundary` | Virtual core instance allocated and initialized. |
| `CoreStarted` | 11 | `CoreLifecycleBoundary` | Virtual core marked Runnable and registered in cluster schedule. |
| `CoreHalted` | 12 | `CoreLifecycleBoundary` | Virtual core transitioned to Halted state via HLT or termination. |
| `CoreEnteredWfi` | 13 | `CoreLifecycleBoundary` | Virtual core executed WFI (Wait For Interrupt) instruction. |
| `CoreEnteredWfe` | 14 | `CoreLifecycleBoundary` | Virtual core executed WFE (Wait For Event) instruction. |
| `CoreResumed` | 15 | `CoreLifecycleBoundary` | Virtual core awakened from WFI (by pending interrupt) or WFE (by event register). |
| `CoreStopped` | 16 | `CoreLifecycleBoundary` | Virtual core shut down (e.g. PSCI CPU_OFF). |

---

## 2. Payload Conventions
For lifecycle events:
- `core_id`: Identifies the affected virtual core.
- `pc`: Architectural PC of the core at the moment of state transition.
- `payload1`: Optional auxiliary context (e.g., wake interrupt ID or PSCI power state code).
- `boundary_type`: Always set to `CoreLifecycleBoundary (9)`.
