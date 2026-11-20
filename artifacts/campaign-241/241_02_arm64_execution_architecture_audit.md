# Stage 241.2: ARM64 Execution Architecture Audit

## 1. Decoder, Execution Dispatch, and Register State
Inspected the CPU core interpreter (`src/cpu/cpu.cpp`) and architectural state structures:

- **Register File**: 31 general-purpose 64-bit registers (`X0` - `X30`), dedicated stack pointer `SP_EL0`, program counter `PC`.
- **NZCV Flags**:
  - `N` (Negative, bit 3): set when MSB of result is 1.
  - `Z` (Zero, bit 2): set when result is 0.
  - `C` (Carry, bit 1): set on unsigned overflow/no borrow.
  - `V` (Overflow, bit 0): set on signed arithmetic overflow.
- **Program Counter Discipline**: Enforces strict 4-byte instruction alignment (`PC % 4 == 0`).
- **Deterministic Trapping**:
  - Unsupported instructions do NOT behave as silent NOPs. They trap into synchronous exception vectors with `ESR_EL1` indicating Undefined Instruction (`EC = 0x00`).
  - No dependence on host-language undefined behavior; signed overflows are checked via unsigned cast comparisons.
