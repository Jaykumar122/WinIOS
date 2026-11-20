# Stage 240.7: Stack and Calling-Convention Conformance

## 1. AAPCS64 Specification Conformance
The guest runtime implements the Procedure Call Standard for the ARM 64-bit Architecture (AAPCS64):

- **Stack Pointer Alignment**: `SP` must remain 16-byte aligned at all public interface boundaries.
- **Argument Passing**:
  - Integer/pointer arguments 1 through 8 passed in registers `X0` - `X7`.
  - Arguments 9 and beyond spilled onto the stack in right-to-left order.
- **Return Values**:
  - Primary scalar/pointer result returned in `X0`.
  - Secondary result returned in `X1`.
- **Register Preservation**:
  - Caller-saved: `X0` - `X18`.
  - Callee-saved: `X19` - `X28`.
  - Frame pointer: `X29`.
  - Link register: `X30`.

## 2. Compiled Workload Conformance
Verified via compiled guest binary `mem_func.macho`:
- Function taking 18 arguments (8 in registers, 10 on stack).
- Deeply recursive stack frames.
- Callee-saved register integrity verified across function invocation boundaries.
