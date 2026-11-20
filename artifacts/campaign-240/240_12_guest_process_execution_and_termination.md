# Stage 240.12: Guest Process Execution and Termination

## 1. Execution Loop & Budget Enforcement
1. **Deterministic Execution Loop**:
   - Steps CPU one instruction at a time or in budgeted blocks.
   - Checks instruction budget counter on every step.
2. **Infinite Loop Prevention**:
   - When instruction count exceeds budget (e.g. 100,000 steps), execution halts and returns `StepResult::kBudgetExceeded`.
3. **Clean Termination**:
   - Invocation of `SYS_exit` (`X16 = 1`) records exit code in task state.
   - Transitions `AppLifecycleState` to `Terminating` then `Terminated`.
   - Invokes `begin_cleanup()` ensuring double-cleanup prevention.
