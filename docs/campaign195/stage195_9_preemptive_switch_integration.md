# Stage 195.9 — Full Preemptive Context-Switch Integration (Tests D & E)

## 1. Executive Summary

Stage 195.9 verified Mandatory Acceptance Tests D and E: complete end-to-end preemptive task switching across multiple independent guest threads scheduled by priority.

---

## 2. Integrated Execution Lifecycle

1. Three guest threads (`task1`, `task2`, `task3`) are created with independent kernel stacks (`0x40000`, `0x50000`, `0x60000`) and distinct priorities (20, 16, 10).
2. Priority scheduler selects Task 1 (highest priority).
3. Task 1 executes, modifies registers (`X0 = 0x1111`, `X1 = 0x2222`), and advances PC.
4. Timer preemption occurs: Task 1's full state is saved into an `ExceptionFrame` on its stack, and the scheduler selects Task 2.
5. Task 2 executes, modifies registers (`X0 = 0x3333`, `X1 = 0x4444`), and advances PC.
6. Timer preemption occurs: Task 2 is saved, Task 3 is selected.
7. Subsequent preemption switches back to Task 1: Task 1 resumes at its exact saved PC (`0x1040`) with registers `X0 = 0x1111` and `X1 = 0x2222` completely intact.
8. Task 2 is resumed at `0x2080` with `X0 = 0x3333` and `X1 = 0x4444` intact.

Evidence Classification: `GUEST_PREEMPTION`, `GUEST_SCHEDULER`, `GUEST_CONTEXT_SWITCH`.
