# WinIOS Campaign 218 — Genuine Guest Execution & Provenance Report

## 1. Executive Summary

Campaign 218 established the first strictly enforced boundary between host execution and genuine guest ARM64 instruction retirement on a Windows 11 x64 host. In prior campaigns, instruction counters were vulnerable to host-side incrementing during simulation steps (such as `GuestRuntimeLifecycle::Step(n)`). Campaign 218 introduced the `ExecutionBoundaryEnforcer` architecture, guaranteeing that only instructions executed and retired directly on the virtual ARM64 CPU engine (`CPU::step()`) increment the verified guest instruction counters.

Furthermore, Campaign 218 verified the complete toolchain pipeline: compiling freestanding C source code with Zig 0.17.0 (`-target aarch64-macos`), generating genuine 64-bit ARM64 Mach-O binaries, loading and validating them via `MachoUserLoader`, enforcing 16 KB page alignment, mapping user address spaces with strict W^X permissions, initializing EL0 architectural CPU state, and retiring real ARM64 instructions.

---

## 2. Execution Boundary Architecture

The boundary architecture is centered around `winios::runtime::ExecutionBoundaryEnforcer` and `ExecutionOriginScope`.

### 2.1 Execution Origins

| Origin Enum | Identifier | Description | Instruction Counting Permitted |
| :--- | :--- | :--- | :--- |
| `kUnknown` | 0 | Default / uninitialized origin | **Rejected** (increments `rejected_increments_`) |
| `kHostSimulation` | 1 | Host C++ classes and mock logic | **Rejected** (increments `rejected_increments_`) |
| `kGuestCpuRetired` | 2 | Instruction retired by virtual CPU `step()` | **Permitted** (increments `verified_guest_instructions_retired_`) |
| `kKernelSyscallService`| 3 | Host-side kernel/syscall emulation code | **Rejected** (increments `rejected_increments_`) |
| `kHostPresentation` | 4 | UI compositor, renderer, window blit | **Rejected** (increments `rejected_increments_`) |

### 2.2 Tamper Resistance Verification

The `stage218_2_boundary_tests` and `stage218_23_adversarial_tests` explicitly verified that:
1. Attempting to record instruction retirements under `kHostSimulation` is rejected with `rejected_increments_++`.
2. Attempting to record instruction retirements under `kKernelSyscallService` or `kHostPresentation` is rejected.
3. Only scopes active with `kGuestCpuRetired` succeed in incrementing `verified_guest_instructions_retired_`.

---

## 3. End-to-End Guest Execution Provenance

### 3.1 Workload Details
- **Source File:** `src/apps/guest_test_workload.c`
- **Compiler:** Zig 0.17.0 (`zig cc -target aarch64-macos -O2 -nostdlib -fno-vectorize -fno-slp-vectorize -Wl,-no_compact_unwind`)
- **Binary Format:** Mach-O 64-bit (`MH_MAGIC_64 = 0xFEEDFACF`)
- **Target Architecture:** `CPU_TYPE_ARM64` (`0x0100000C`), `CPU_SUBTYPE_ARM64_ALL`
- **Segments:**
  - `__PAGEZERO`: `vmaddr = 0x000000000`, `vmsize = 0x100000000` (4 GB, `VM_PROT_NONE`)
  - `__TEXT`: `vmaddr = 0x100000000`, `vmsize = 0x4000` (16 KB, `VM_PROT_READ | VM_PROT_EXECUTE`)
  - `__DATA_CONST`: `vmaddr = 0x100004000`, `vmsize = 0x4000` (16 KB, `VM_PROT_READ`)
  - `__DATA`: `vmaddr = 0x100008000`, `vmsize = 0x4000` (16 KB, `VM_PROT_READ | VM_PROT_WRITE`)

### 3.2 Computational Proof of Real Execution
The guest workload executed on the virtual CPU engine calculates:
```c
int result = factorial(5) + compute_math(10, 8) + test_memory_ops();
```
Where:
- `factorial(5)`: Recursive function with 5 stack frame allocations (`sub sp, sp, #32`, `stp x29, x30`, `ldp x29, x30`, `add sp, sp, #32`, `ret`), computing `5 * 4 * 3 * 2 * 1 = 120`.
- `compute_math(10, 8)`: Arithmetic operations using `madd`, `sub`, `lsl`, computing `120`.
- `test_memory_ops()`: Local stack array initialization, store operations (`str w`, `stp`), loop summation, and load operations (`ldr w`), computing `100`.
- **Expected Total:** `120 + 120 + 100 = 340`.
- **Observed X0 Register:** `340` (exact architectural match).
- **Verified Instruction Retirement Count:** Bounded, deterministic execution matching instruction cycle count.
