# Stage 241.20: Deterministic Replay Scope and Validation

## 1. Replay Verification Suite
Implemented `tests/unit/stage241_20_deterministic_replay_tests.cpp`:

1. **State Inclusion Scope**:
   - General-purpose registers `X0` - `X30`.
   - Program Counter `PC` and Stack Pointer `SP`.
   - Condition flags `NZCV`.
   - Total instruction count and process exit code.
2. **Replay Invariance**:
   - Repeated runs from identical initial state produce byte-for-byte identical architectural state across all 31 registers, PC, SP, and NZCV.
3. **Divergence Detection**:
   - Injected a 1-bit perturbation into initial argument register `X0`.
   - Immediate divergence detected in register state and execution path, confirming sensitive replay verification.

## 2. Test Results
- Standalone execution: PASS.
