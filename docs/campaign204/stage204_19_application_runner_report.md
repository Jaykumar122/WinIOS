# Stage 204.19 — Application Runtime Entry and Exit Verification Report

## 1. Executive Summary
- **Stage:** 204.19
- **Feature:** Integrated Application Runtime Lifecycle (`DarwinApplicationRunner`).
- **Target Device:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`).
- **Status:** **PASS** (100% verified in Release and Debug builds with zero compiler warnings under `/W4 /WX /permissive-`).

## 2. Design and Architecture
Stage 204.19 implements `DarwinApplicationRunner`, unifying all foundational subsystems established across Campaign 204 into a bounded, clean-room ARM64 Mach-O execution pipeline:
1. **Binary & Dependency Loading:**
   - Evaluates Mach-O headers, loads segments (`__PAGEZERO` unmapped, `__TEXT` RX, `__DATA` RW, `__LINKEDIT` R).
   - Registers images with `DyldEngine` and tracks dynamic dependencies (`LC_LOAD_DYLIB`).
2. **Linker & Initializer Execution:**
   - Staged synthetic `libSystem.B.dylib` runtime stubs into guest memory.
   - Discovers and computes bottom-up topological initializers (`compute_topological_initializers`).
3. **Process & Thread Staging:**
   - Allocates guest thread stack with a 16KB `PROT_NONE` guard page via `DarwinThreadRuntime::allocate_thread_stack`.
   - Staged Darwin ABI arguments (`argc`, `argv[]`, `envp[]`, `apple[]`) via `DarwinProcessStartup`.
   - Formats `pthread_t` control block in guest memory with TSD slots and binds system registers `TPIDRRO_EL0` and `TPIDR_EL0`.
4. **Guest Instruction Loop & Syscall Dispatch:**
   - Resets CPU, configures EL0 userspace mode with `CPACR_EL1.FPEN = 0b11` (full FP/SIMD access).
   - Traps Darwin `SVC #0x80` syscalls and routes them through `DarwinSyscallDispatcher`.
   - Mediates bounded execution with configurable `max_instructions`.
   - Traps out-of-bounds execution and CPU faults with informative diagnostics (`"CPU_Fault"`, `"OutOfBoundsPC"`).
   - Detects process termination via `SYS_exit` and captures exit code and stdout buffers.

## 3. Test Coverage Summary
All tests executed and passed in `tests/unit/stage204_19_application_runner_tests.cpp`:
- `test_minimal_exit42_binary`: PASS (Executes synthetic Mach-O binary, exits with code 42 via `SYS_exit`).
- `test_multi_segment_write_and_exit`: PASS (Multi-segment Mach-O execution, calls `SYS_write(1, "Hello, Darwin!\n", 15)`, verifies stdout capture, cleanly exits with code 0).
- `test_stack_arguments_and_environment`: PASS (Verifies Darwin ABI initial stack frame staging with `argv` and `envp`).
- `test_max_instructions_bounded_execution`: PASS (Infinite loop binary gracefully halted after reaching `max_instructions=100`, reporting `MaxInstructionsReached`).
- `test_cpu_fault_out_of_bounds`: PASS (Misaligned / out-of-bounds jump triggers structured CPU fault without crashing).

## 4. Verification Evidence
- Release Build: 0 errors, 0 warnings.
- Debug Build: 0 errors, 0 warnings.
- Test Status: 5/5 test suites passed.
