# Campaign 194 Stage 194.13: Guest Syscall Entry & Dispatch

## Objective
Establish an architectural syscall gateway between EL0 userspace and EL1 synthetic kernel runtime:
1. `GuestSyscallDispatcher`:
   - Guest EL0 `SVC #0` exception vectors to `VBAR_EL1 + 0x400` (Lower EL Synchronous Exception).
   - ESR_EL1 syndrome: EC = `0x15` (`ESR_EC_SVC64`), ISS contains immediate (`0` for `SVC #0`).
   - Registers X0..X7 contain syscall arguments, with syscall number in X8 (standard ABI) or X16 (Darwin Mach trap/BSD convention).
   - Standard dispatch mapping:
     - `SYS_YIELD` (1): Voluntary yield to scheduler.
     - `SYS_SLEEP` (2): Sleep thread.
     - `SYS_CREATE_THREAD` (3): Spawn new thread with entry point, stack, priority.
     - `SYS_EXIT` (4): Terminate current thread with exit code.
     - `SYS_GET_TID` (7): Return current thread ID.
     - `SYS_TIME_TICKS` (8): Return cumulative system ticks.
   - Return value placed in X0.
   - Vector handler saves user state, dispatches syscall, restores state, executes `ERET` back to EL0.
2. Synthetic guest vector handler:
   - Machine-code stub `emit_vector_stub_eret()` installed at `VBAR_EL1 + 0x400`.

## Implementation Details
- Created `src/kernel/syscall_dispatcher.h` and `src/kernel/syscall_dispatcher.cpp`:
  - `SyscallId` enumeration covering synthetic OS services.
  - `KernelSyscallDispatcher`: Registers built-in handlers for thread creation, yielding, sleeping, thread termination, TID query, and tick query.
  - Decodes X8 or X16 to identify system call number and extracts X0..X5 arguments.
  - Places result in X0 and routes through scheduler and thread manager.
  - `emit_vector_stub_eret`: Emits authentic ARM64 `ERET` (`0xD69F03E0u`) for installation at `VBAR_EL1 + 0x400`.
- Authored test suite `tests/unit/stage194_13_syscall_dispatch_tests.cpp`.

## Test Results
- `stage194_13_syscall_dispatch_tests`:
  - `test_svc_vectoring_el0_to_el1`: PASSED (EL0 -> EL1 transition, ESR_EL1 EC=0x15, ELR_EL1 return address, VBAR_EL1 + 0x400 target).
  - `test_syscall_parameter_extraction_and_dispatch`: PASSED (X8 parameter extraction, TID and tick queries).
  - `test_syscall_create_thread`: PASSED (User thread spawn with custom priority and entry point).
  - `test_eret_transition_back_to_el0`: PASSED (ERET transition from EL1 back to EL0).
  - `test_full_roundtrip_user_syscall_execution`: PASSED (Full end-to-end user loop: MOV -> SVC #0 -> EL1 dispatch -> ERET -> EL0 return).
  - Total: 30 passed, 0 failed.

## Evidence Classification
- Architectural SVC handling: `GUEST_SYSCALL`
- Privilege transition: `GUEST_EL0` / `GUEST_EL1`
- Dispatcher logic: `SYNTHETIC_KERNEL`
