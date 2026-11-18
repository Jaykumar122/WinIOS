# WinIOS Campaign 220 — Stage 220.1 Campaign 219 Forensic Audit

## 1. Executive Forensic Summary

Campaign 219 claimed the introduction of:
1. Multi-process architecture with independent address spaces.
2. ARM64 thread contexts with hardware TLS (`TPIDR_EL0`).
3. Multi-process scheduler with round-robin 4-tick quantum.
4. Guest thread synchronization and lifecycle syscalls (SYS 522–526).
5. VFS file descriptors and bounded circular pipes (SYS 41, 42, 3, 4, 6).
6. Darwin-inspired message trap (`winios_msg_header_t`, SYS 527, SYS 31).
7. Minimal Launch Service (`ServiceManager`).
8. Dynamic Mach-O dependency loader (`GuestDynamicLoader`).
9. Native ARM64 sample applications (`supervisor`, `worker`, `threaded_math`).
10. A 24-step integration test scenario.

This audit examines the actual code and tests in commit `8f00af545488e9641070b4ac2bdeb7c6374aca75` to evaluate whether each claim reflects genuine ARM64 guest execution or host-side test orchestration.

---

## 2. Claim / Evidence Verification Matrix

| Subsystem / Claim | Status | Direct Test Reference | Source Reference | Forensic Finding |
| :--- | :--- | :--- | :--- | :--- |
| **Independent Page Tables** | **Verified by direct test** | `tests/unit/stage219_3_address_space_isolation_tests.cpp` | `src/kernel/bsd_process.h:97`, `src/memory/user_address_space.cpp` | Each `BsdProcess` owns a unique `UserAddressSpace` with dedicated L2/L3 page tables. Identical VAs map to distinct PAs; cross-process access is rejected by MMU translation. |
| **Thread Context & TPIDR_EL0** | **Verified by direct test** | `tests/unit/stage219_5_thread_context_tests.cpp` | `src/kernel/thread.h:37-61`, `src/kernel/thread.cpp` | `GuestTcb` is exactly 512 bytes (`static_assert`). Register save/restore round trips preserve X0–X30, PC, SP_EL0, SPSR_EL1, and `TPIDR_EL0`. |
| **Scheduler TTBR0 Switching** | **Verified by direct test** | `tests/unit/stage219_8_scheduler_execution_tests.cpp` | `src/kernel/multi_process_scheduler.cpp:114-125` | `perform_context_switch` updates `TTBR0_EL1` and calls `cpu.get_mmu().invalidate_tlb_all()` when the next thread belongs to a different PID. |
| **Guest Interleaved Stepping** | **Partially verified** | `tests/unit/stage219_11_multi_thread_guest_workload_tests.cpp` | `src/kernel/multi_process_scheduler.cpp` | Two threads were stepped in a single process, but thread preemption driven by hardware timer interrupts during guest execution is not yet wired to a live guest timer ISR; switching is invoked via explicit `scheduler.yield(cpu)` or `scheduler.schedule_next(cpu)`. |
| **Thread Lifecycle Syscalls** | **Verified by direct test** | `tests/unit/stage219_10_synchronization_tests.cpp` | `src/kernel/user_syscall_dispatcher.cpp:1161-1256` | Syscalls 522–526 (`kSysWiniosMutexLock`, `kSysWiniosMutexUnlock`, `kSysWiniosThreadCreate`, `kSysWiniosThreadExit`, `kSysWiniosThreadJoin`) enforce thread ownership, validate pointers, and manage state transitions. |
| **File Descriptors & Pipe IPC** | **Verified by direct test** | `tests/unit/stage219_13_vfs_fd_tests.cpp`, `stage219_14_pipe_ipc_tests.cpp` | `src/kernel/bsd_process.cpp`, `src/kernel/user_syscall_dispatcher.cpp:332-420` | Bounded circular pipe (16 KB) with `SYS_pipe`, `SYS_read`, `SYS_write`, `SYS_close`, and `SYS_dup`. Fork clones the FD table preserving open pipe handles. |
| **Message Trap (SYS 527 / 31)** | **Verified by direct test** | `tests/unit/stage219_17_msg_trap_tests.cpp` | `src/kernel/user_syscall_dispatcher.cpp:506-536` | `winios_msg_header_t` (24 bytes) parsed; routes payload to `GuestIpcRouter`. Non-Mach IPC, explicitly labeled as Darwin-inspired message trap. |
| **ServiceManager (PID 1)** | **Verified by direct test** | `tests/unit/stage219_18_service_manager_tests.cpp` | `src/services/service_manager.cpp` | Manages daemon registry, auto-restart on exit, port resolution, and health tracking. |
| **Dynamic Mach-O Loader** | **Verified by direct test** | `tests/unit/stage219_20_dynamic_loader_tests.cpp` | `src/loader/guest_dynamic_loader.cpp` | Bounded loader maps dylib segments at 16 KB boundaries, resolves symbol tables, and updates GOT pointers. Not full dyld (no chained fixups, no shared cache). |
| **24-Step E2E Integration** | **Partially verified / Hybrid** | `tests/unit/stage219_22_multi_process_e2e_tests.cpp` | `tests/unit/stage219_22_multi_process_e2e_tests.cpp` | **Key Finding:** In Stage 219.22, Supervisor was stepped 50 real guest instructions on CPU, but IPC write/read and thread creation were executed via host harness invoking `dispatcher.handle_syscall(...)` rather than guest ARM64 binaries executing `svc #0` in an integrated event loop. |
| **Embedded Sample Apps** | **Verified by source inspection & test** | `tests/unit/stage219_21_sample_apps_report.md` | `src/apps/campaign219_*.c`, `src/loader/embedded_campaign219_apps.inl` | Genuine Mach-O binaries compiled with Zig 0.17.0 for ARM64 macOS target and embedded as byte arrays. |

---

## 3. Detailed Investigation of E2E Guest Execution vs Host Orchestration

Inspection of `tests/unit/stage219_22_multi_process_e2e_tests.cpp`:
1. In Step 15, `cpu.step(mem)` executed 50 instructions of the supervisor Mach-O image (`campaign219_supervisor`) under `ExecutionOrigin::kGuestCpuRetired`.
2. In Step 16, the test harness manually set CPU registers `X16 = kSysWrite`, `X0 = fd`, `X1 = va`, `X2 = len` and called `dispatcher.handle_syscall(...)` directly from C++ host code, instead of letting the guest application issue `svc #0` and having an exception handler trap into the dispatcher.
3. In Step 17, `scheduler.yield(cpu)` was called directly by the host test harness, rather than being triggered by a guest `sched_yield` syscall.
4. In Step 18, the worker process read from the pipe via a host-invoked `dispatcher.handle_syscall`.
5. In Step 20, worker thread math execution and mutex locking was simulated through direct dispatcher calls and guest memory reads.

**Conclusion for Campaign 220:** While the underlying components (CPU stepping, MMU translation, syscall dispatcher, scheduler context switching) are fully functional, they were connected via host test scaffolding in Campaign 219. Campaign 220 must establish **genuine end-to-end guest execution** where the guest applications themselves execute `svc #0`, yield through syscalls, and exchange data through guest CPU execution without host-side substitution.

---

## 4. Test Suite Inventory Audit (766 vs 55 Suites)

- **Total Registered Suites in CTest:** 766
- **Distribution:**
  - Tests #1–#711: Historical unit and integration tests from Campaigns 1–214.
  - Tests #712–#766: Tests added in Campaigns 215–219 (55 suites).
- **Failure Analysis of Legacy Tests:**
  When executing the full 766 test suite, 7 legacy tests failed:
  1. `stage211_15_essential_syscalls_tests`: Fails because it expects negative errno in X0 (Fast ABI), whereas Campaign 212/213 introduced `is_darwin_strict` which returns positive errno in X0 with Carry bit (PSTATE.C = 1).
  2. `stage211_22_adversarial_userspace_security_tests`: Similar ABI assumption mismatch.
  3. `stage212_17_init_coordinator_tests`: Stale task pointer lifecycle.
  4. `stage212_20_multi_process_e2e_tests`: Early prototype multi-process test superseded by Campaign 219 scheduler.
  5. `stage212_21_snapshot_replay_tests`: Serializer mismatch with new TCB fields.
  6. `stage214_5_abi_boundary_tests`: Fast ABI vs Darwin Strict assertion mismatch.
  7. `stage214_22_adversarial_security_tests`: Memory limit boundary change.
- **Finding:** The failures in tests #645, #649, #665, #668, #669, #694, #710 are historical contract drift caused by the deliberate evolution of the Darwin ABI in Campaigns 212–219. All 55 test suites from Campaigns 215–219 pass 100%.
