# WINIOS CAMPAIGN 212 — STAGE 212.0 BASELINE AUDIT

**Campaign:** 212  
**Baseline Commit:** `9ee8978b54e743945a0b6b547cd1ff5ae2bdc315`  
**Active Branch:** `campaign-191r`  
**Host:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Translation Granule:** 16 KB  
**Pinned Public XNU Source:** `xnu-10002.1.13`  
**Date:** 2026-11-17  

---

## 1. Baseline Verification & Integrity

The starting baseline commit was confirmed as `9ee8978b54e743945a0b6b547cd1ff5ae2bdc315` on branch `campaign-191r`.

### Protected Assets Verification:
- `D:\WinISO26\WinIOS_README.md`: `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` (VERIFIED EXACT)
- `D:\WinISO26\WinIOS\docs\progress.md`: `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` (VERIFIED EXACT)
- `D:\WinISO26\Inferno-Research/`: VERIFIED UNTOUCHED

---

## 2. Inventory of Existing Userspace Subsystems (from Campaign 211)

1. **Architectural Trap Frame (`ArmTrapFrame` & `TrapFrameManager`):**
   - 304-byte packed struct (`src/kernel/arm_trap_frame.h`, `src/kernel/arm_trap_frame.cpp`).
   - Captures: X0..X30, SP_EL0, ELR_EL1, SPSR_EL1, ESR_EL1, FAR_EL1.
   - Machine code routines: `emit_el0_trap_entry()`, `emit_el0_trap_exit()`.

2. **EL0 Execution & Exception Vectoring:**
   - Vector table base at `VBAR_EL1`.
   - Lower EL AArch64 Synchronous vector slot at `VBAR_EL1 + 0x400`.
   - `svc #0` triggers synchronous exception (`ESR_EL1.EC = 0x15`).

3. **BSD Process Subsystem (`BsdProcess`, `BsdProcessManager`):**
   - Process states: `Starting` (`SIDL`), `Runnable` (`SRUN`), `Sleeping` (`SSLEEP`), `Stopped` (`SSTOP`), `Zombie` (`SZOMB`).
   - Monotonic PID allocation: PID 0 (kernel), PID 1 (init/launchd).
   - `FileDescriptorTable`: Bounded table per process (stdin=0, stdout=1, stderr=2).

4. **User Address Space (`UserAddressSpace`):**
   - Manages Stage-1 translation table root (`root_l2_table_pa_`) with 16KB granule.
   - Bounded virtual address space: `kUserSpaceLimitVa = 0x80000000ULL` (2 GiB).
   - Safe user pointer validation: `validate_user_buffer()`, `copyin()`, `copyout()`.

5. **Mach-O User Loader (`MachoUserLoader`):**
   - Validates `MH_MAGIC_64` (0xFEEDFACF) and `CPU_TYPE_ARM64` (0x0100000C).
   - Parses `LC_SEGMENT_64` (`__PAGEZERO`, `__TEXT`, `__DATA`, `__DATA_CONST`), `LC_MAIN`, and `LC_UNIXTHREAD`.
   - Enforces W^X security policy.

6. **AAPCS64 User Stack (`UserStackBuilder`):**
   - Maps 64 KiB stack at `0x7FFE0000`–`0x7FFF0000`.
   - Sets up initial stack layout: string pool, NULL-terminated `envp` and `argv` pointer tables, `argc`.
   - Enforces 16-byte stack pointer alignment.

7. **System Call Dispatcher (`UserSyscallDispatcher`):**
   - Dual-mode calling convention: Darwin XNU ABI (`X16` syscall number, `X0..X5` arguments, `X0` return) and WinIOS Fast ABI (`X0` syscall number).
   - Implemented: `SYS_exit` (1), `SYS_write` (4), `SYS_getpid` (20), `SYS_mach_msg_trap` (31), `SYS_getppid` (39), `SYS_sched_yield` (329).

8. **Mach IPC (`ProcessIpcSpace`):**
   - Per-process port name space with rights: `Receive`, `Send`, `SendOnce`, `PortSet`, `DeadName`.
   - Special ports: `TASK_PORT` (1), `TASK_BOOTSTRAP_PORT` (2), `TASK_HOST_PORT` (3).

9. **Userspace Coordinator (`UserspaceCoordinator`):**
   - Full launch pipeline: PPM allocation, UAS creation, Mach-O loading, stack building, thread creation, MMU Stage-1 activation (TCR_EL1, MAIR_EL1, TTBR0_EL1, SCTLR_EL1), exception vectoring (`VBAR_EL1`), stepping, and syscall interception.

---

## 3. Baseline Test Verification

- Full test suite: **650 registered tests**.
- Result: **649 passed, 1 skipped (`inferno_integration_tests`), 0 failed**.
- Total execution time: 90.97 seconds.
- Compiler warnings: 0 under `/W4 /WX /permissive-`.

---

## 4. Objectives for Campaign 212

1. Expand `BsdProcessManager` to support full parent/child hierarchy, process spawning, child tracking, exit propagation, and `wait4()` reaping.
2. Implement bounded Mach-O dynamic loader (`GuestDynamicLoader`) capable of resolving dependencies (`LC_LOAD_DYLIB`), imported symbols, and ARM64 relocations between guest images.
3. Construct a minimal freestanding ARM64 runtime library (`libwinios_runtime.dylib` / Mach-O).
4. Extend VFS with virtual filesystem directory traversal, file operations (`open`, `close`, `read`, `write`, `stat`, `fstat`), and inter-process unidirectional `pipe`.
5. Implement Mach Bootstrap Service Registry inspired by public Mach bootstrap (`bootstrap_register`, `bootstrap_look_up`).
6. Build a complete multi-process workload featuring PID 1 init coordinator, child service process, and client application communicating via Mach IPC and pipes.
7. Implement 25-vector adversarial security hardening and full snapshot/replay.
