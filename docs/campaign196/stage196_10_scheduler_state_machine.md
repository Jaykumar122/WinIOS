# Stage 196.10 — Scheduler State-Machine Integrity (Acceptance Tests G & H)

## 1. Architectural Invariants
- **Thread State Transitions:**
  - Valid: `Ready -> Running`, `Running -> Ready`, `Running -> Blocked`, `Blocked -> Ready`, `Running -> Terminated`, `Ready -> Terminated`.
  - Prohibited: `Blocked -> Running` (must unblock to Ready first), `Terminated -> Running` (dead thread resurrection prohibited).
  - Enforced via `KernelThread::transition_state()` with audit history logging.
- **SMP Core Isolation (Acceptance Test H):**
  - In `SmpScheduler`, a thread may only be executed by at most one virtual core at any given time:
    $$\forall c_1 \neq c_2, \quad \text{current}(c_1) \neq \text{current}(c_2) \quad (\text{if non-null, non-idle})$$
  - Run-queue invariant: A thread currently executing on a core is removed from all ready run-queues.

## 2. Test Evidence (Acceptance Tests G & H)
Verified via `stage196_9_scheduler_integrity_tests`:
- Test G1: State transition legality assertions and illegal transition rejection.
- Test G2: State history tracking across thread lifecycle.
- Test H1: Multi-core SMP load balancing & ticking over 100 rounds with continuous duplicate-thread detection assertion.

## 3. Evidence Classification
`SYNTHETIC_KERNEL`, `GUEST_SMP`. Tested in Debug and Release.
