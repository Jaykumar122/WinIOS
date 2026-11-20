# Stage 241.21: Exception and Adversarial Execution Verification

## 1. Exception Trapping & Safety
- **Data Abort**: Accessing unmapped memory addresses safely traps into synchronous exception handler with `ESR_EL1 = 0x92000007` and does not crash the host emulator.
- **Instruction Budget Enforcement**: Infinite loops (e.g. `B .`) are halted safely when instruction counter reaches budget limit, returning `budget_exhausted = true`.
- **Undefined Instructions**: Invalid opcodes (`0x00000000`) trapped deterministically.
