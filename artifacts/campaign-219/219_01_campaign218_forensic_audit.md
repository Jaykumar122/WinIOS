# WinIOS Campaign 219 — Stage 219.1 Campaign 218 Forensic Audit

## 1. Executive Summary

This forensic audit evaluates the implementation delivered in Campaign 218 (`28ee0df5928e897a7fb142abe317bdc98e33057c`) to provide an evidence-based baseline for Campaign 219.

---

## 2. Investigation of the Reported `X0 == 340` Result

In Campaign 218, execution tests validated that the virtual CPU returns register `X0 == 340` after executing the freestanding guest workload. Forensic analysis of the source code (`src/apps/guest_test_workload.c`) and compiler assembly establishes the exact component breakdown:

```c
int entry_point(void) {
    int f = factorial(5);        // Component 1: Recursion
    int m = compute_math(10, 8); // Component 2: Arithmetic/Bitwise
    int s = test_memory_ops();   // Component 3: Stack Memory
    return f + m + s;            // 120 + 120 + 100 = 340
}
```

### 2.1 Component 1: `factorial(5)`
- Implementation: Recursive call chain with 5 stack frame allocations (`stp x29, x30, [sp, #-32]!`, `ldp x29, x30, [sp], #32`, `ret`).
- Mathematical value: `5! = 5 * 4 * 3 * 2 * 1 = 120`.

### 2.2 Component 2: `compute_math(10, 8)`
- Implementation: Multi-operation arithmetic and bitwise pipeline:
  - `(a * 3) + (b >> 1) = (10 * 3) + (8 >> 1) = 30 + 4 = 34` (`0x22`)
  - `res ^= 0x5A = 34 ^ 90 = 0x22 ^ 0x5A = 0x78 = 120`.
- Mathematical value: `120`.

### 2.3 Component 3: `test_memory_ops()`
- Implementation: Allocates a 4-element volatile array on the thread stack, writes `10, 20, 30, 40` using `str w`, then loops reading them back with `ldr w` and accumulating the sum.
- Mathematical value: `10 + 20 + 30 + 40 = 100`.

### 2.4 Total Architectural Return State
`X0 = 120 + 120 + 100 = 340`.
This verifies not only branch execution, but function calling convention, link register handling (`LR` / `X30`), frame pointer chaining (`FP` / `X29`), stack pointer arithmetic (`SP`), volatile memory stores/loads through the MMU, and register return conventions.

---

## 3. Execution Boundary & Instruction Retirement Audit

### 3.1 `ExecutionBoundaryEnforcer` Implementation
The execution boundary is implemented in `src/runtime/execution_boundary.h` and `src/runtime/execution_boundary.cpp`.
- `current_origin_`: Tracks whether execution is currently inside `kGuestCpuRetired`, `kHostSimulation`, `kKernelSyscallService`, or `kHostPresentation`.
- `RecordGuestInstructionRetirement(count)`:
  - If `current_origin_ != ExecutionOrigin::kGuestCpuRetired`, the increment is **rejected**, and `rejected_increments_` is incremented.
  - If `current_origin_ == ExecutionOrigin::kGuestCpuRetired`, `verified_guest_instructions_retired_` is updated.
- `ExecutionOriginScope`: RAII guard setting and restoring the origin.

### 3.2 Audit Findings
- **Proven:** Host-side simulation or thunking code cannot increment the verified retirement counter.
- **Limitation Identified:** Currently, instruction stepping is executed serially in test harnesses. Campaign 219 must integrate the boundary enforcer with per-thread retirement accounting in a multi-threaded scheduler.

---

## 4. Subsystem Forensic Status

1. **Mach-O Loader (`MachoUserLoader`):**
   - Successfully validates 64-bit headers, enforces 16 KB page alignment, rejects overlapping segments, and strictly enforces W^X.
   - Limitation: Rejects dynamic Mach-O commands (`LC_LOAD_DYLIB`, `LC_LOAD_DYLINKER`).
2. **User Virtual Memory (`UserAddressSpace`):**
   - Successfully isolates user address space up to 32 GiB (`0x800000000ULL`).
   - Limitation: Currently lacks copy-on-write (COW) or cross-process memory sharing primitives.
3. **Syscall Dispatcher (`UserSyscallDispatcher`):**
   - Successfully dispatches Category C syscalls (513–521) with copyin/copyout validation and path traversal defenses.
   - Limitation: Does not yet have POSIX file descriptor pipes or structured Mach-message trap handling.
4. **Process & Thread Management:**
   - `BsdProcess` tracks basic state and threads, but threads do not have independent user CPU register contexts and scheduling quanta.
