# Stage 240.15: Minimal Compiled ARM64 Guest Applications

## 1. Compiled Workload Portfolio
Four genuine freestanding ARM64 Mach-O binaries were compiled using Zig 0.17.0 and verified under the WinIOS runtime:

1. **`arith_control` (`main.c`)**:
   - Computes recursive Fibonacci and complex arithmetic.
   - Verifies loop invariants and branch predictions.
   - Expected exit code: **42**. Verified: **42**.
2. **`mem_func` (`main.c`)**:
   - Exercises AAPCS64 calling conventions with 18 arguments (8 in registers, 10 on stack).
   - Executes recursive calls and memory buffer validation.
   - Expected exit code: **55**. Verified: **55**.
3. **`syscall_lifecycle` (`main.c`)**:
   - Negotiates Category C ABI v1.1 (`0x00010100`).
   - Executes `SYS_getpid` (20) and `SYS_getuid` (24).
   - Expected exit code: **77**. Verified: **77**.
4. **`multi_app_worker` (`main.c`)**:
   - Performs concurrent worker calculations.
   - Expected exit code: **88**. Verified: **88**.

## 2. Test Verification
All workloads executed through loader, memory mapping, CPU interpreter, and syscall dispatcher in `tests/unit/stage240_15_compiled_guest_workload_tests.cpp`.
