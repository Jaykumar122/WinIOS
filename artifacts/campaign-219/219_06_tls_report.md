# WinIOS Campaign 219 — Stage 219.6: Thread-Local Storage (TLS) Architecture

## 1. Architectural System Register: `TPIDR_EL0`

In the ARM64 architecture, user-mode Thread-Local Storage (TLS) is anchored by the `TPIDR_EL0` system register (Thread ID Register, EL0).

- **Sysreg Encoding**: `op0=3, op1=3, CRn=13, CRm=0, op2=2` (`kSysregTPIDR_EL0 = 0x5E82`).
- **Access Privilege**: Readable and writable at Exception Level 0 (EL0) and Exception Level 1 (EL1).
- **Instruction Access**:
  - `mrs Xt, tpidr_el0` (`0xD53BD040 | t`): Reads TLS pointer into general register `Xt`.
  - `msr tpidr_el0, Xt` (`0xD51BD040 | t`): Writes TLS pointer from general register `Xt`.

## 2. Kernel Context Preservation

To support preemptive and cooperative multi-threading, the kernel must guarantee that each thread's `TPIDR_EL0` value remains isolated across context switches:

1. `KernelThread::save_context_from_cpu`:
   Reads `cpu.get_tpidr_el0()` and commits it to `tcb_.tpidr_el0`.
2. `KernelThread::load_context_to_cpu`:
   Restores `tcb_.tpidr_el0` into `cpu.set_tpidr_el0()`.
3. `MultiProcessScheduler::perform_context_switch`:
   Saves active thread's `cpu.get_tpidr_el0()` and loads next ready thread's `tcb().tpidr_el0`.

## 3. Test Evidence

Validated in `stage219_5_thread_context_tests.exe` and `stage219_11_multi_thread_guest_workload_tests.exe`:
- Thread 1 assigned TLS base `0x100`.
- Thread 2 assigned TLS base `0x200`.
- Both threads executed ARM64 instructions `mrs x1, tpidr_el0` in EL0.
- Context switches preserved respective TLS bases without cross-thread contamination.
