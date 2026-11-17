# WINIOS CAMPAIGN 211 FINAL ENGINEERING REPORT
## BSD Process Initialization, Mach IPC Expansion & First Userspace Mach-O Execution

**Campaign:** 211  
**Stages Completed:** 211.0 – 211.24 (25 sequential stages)  
**Target Research Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Physical Topology:** 6 cores (2 Everest Performance + 4 Sawtooth Efficiency)  
**Translation Granule:** 16 KB  
**Pinned Public XNU Source:** `xnu-10002.1.13`  
**Host Environment:** Windows 11 x64  
**Primary Toolchain:** Visual Studio 2026 / MSVC C++20 (`/W4 /WX /permissive-`)  
**Cross Compiler:** Zig 0.17.0 (`aarch64-macos`)  
**Git Branch:** `campaign-191r`  
**Baseline Commit:** `7ac82f9b6a02439db58d2bdf5d20d72023bb64dd`  
**Date:** 2026-11-17  

---

## 1. EXECUTIVE SUMMARY

Campaign 211 marks an unprecedented architectural milestone in the WinIOS project: **the successful, reproducible execution of genuine 64-bit ARM64 Mach-O userspace applications at Exception Level 0 (EL0) backed by genuine Stage-1 16KB page tables and a BSD process subsystem.**

Progressing directly from Campaign 210's kernel thread and task foundations, Campaign 211 established the complete boundary between unprivileged userspace (`EL0`) and privileged Darwin/XNU kernel space (`EL1`):

1. **Complete ARM64 Architectural Context Preservation:** Implemented a packed 304-byte `ArmTrapFrame` capturing all 31 general-purpose registers (X0..X30), user stack pointer (SP_EL0), return address (ELR_EL1), program status (SPSR_EL1), exception syndrome (ESR_EL1), and fault address (FAR_EL1), with authentic machine-code entry and exit sequences (`ERET`).
2. **Strict EL0 Privilege Separation:** Validated architectural exception vectoring through `VBAR_EL1 + 0x400` upon `svc #0` execution, complete with PSTATE mode preservation and unprivileged register trapping.
3. **BSD Process Architecture & Lifecycle:** Modeled and implemented authentic XNU `proc_t` semantics (`BsdProcess`), monotonic PID allocation, lifecycle state transitions (`SIDL` -> `SRUN` -> `SZOMB`), bounded file descriptor tables (`FileDescriptorTable`), and per-process Mach IPC spaces (`ProcessIpcSpace`).
4. **User Address-Space Management (`UserAddressSpace`):** Constructed isolated user virtual memory spaces backed by TTBR0_EL1 Stage-1 16KB translation tables, bounded user limits (`0x00000000` to `0x80000000`), copyin/copyout safety barriers, and user buffer validation.
5. **Freestanding ARM64 Mach-O Loading:** Engineered `MachoUserLoader` supporting `MH_MAGIC_64` (0xFEEDFACF), parsing `LC_SEGMENT_64` (`__PAGEZERO` null-dereference guard, `__TEXT` RX, `__DATA` RW), `LC_MAIN`, and `LC_UNIXTHREAD`, while enforcing strict W^X security.
6. **AAPCS64 User Stack Construction:** Implemented `UserStackBuilder` setting up initial userspace stack frames (argc, argv, envp, string pools) aligned to 16 bytes.
7. **Dual-Mode System Call Dispatcher:** Implemented standard Darwin XNU calling conventions (`X16` syscall number, `X0..X5` arguments, `X0` return) and WinIOS Fast ABI, handling `SYS_exit` (1), `SYS_write` (4), `SYS_getpid` (20), `SYS_mach_msg_trap` (31), `SYS_getppid` (39), and `SYS_sched_yield` (329).
8. **End-to-End Execution of Genuine Applications:** Loaded and executed two freestanding Mach-O binaries (`user_first_app.macho` and `user_init_app.macho`) compiled with Zig 0.17.0, verifying deterministic instruction retirement, syscall handling, and console output capture.
9. **Full Verification & Zero Regressions:** 13/13 Stage 211 test suites passed 100%. The full project regression suite achieved 649/650 passed (1 skipped `inferno_integration_tests` as expected, 0 failures) with zero warnings under `/W4 /WX /permissive-`.

---

## 2. STAGE-BY-STAGE ENGINEERING PROGRESSION

### Stage 211.0 — Baseline Integrity & Safety Audit
- Verified active branch: `campaign-191r`.
- Verified starting baseline commit: `7ac82f9b6a02439db58d2bdf5d20d72023bb64dd` (Campaign 210 closeout).
- Audited protected assets: `WinIOS_README.md`, `docs/progress.md`, and `Inferno-Research/` matched SHA-256 hashes bit-for-bit.
- Established working directories: `docs/campaign211/` and `artifacts/campaign-211/`.
- Full baseline CTest executed: 636 passed, 1 skipped, 0 failed.

### Stage 211.1 — Campaign 210 Independent Audit & Boundary Identification
- Conducted independent audit of Campaign 210 deliverables.
- Identified the two critical gates required for genuine EL0 userspace execution:
  - *Gate A (Context Switch):* Cooperative kernel thread context switching vs asynchronous trap frame context preservation.
  - *Gate B (Privilege & MMU Separation):* Dual-translation regime (TTBR0 for EL0 user space, TTBR1 for EL1 kernel space) and exception vectoring via `VBAR_EL1 + 0x400`.

### Stage 211.2 — ARM64 Context Preservation Architecture
- Designed and implemented `ArmTrapFrame` (304-byte packed, 16-byte aligned) in `src/kernel/arm_trap_frame.h` and `src/kernel/arm_trap_frame.cpp`.
- Implemented `TrapFrameManager` with `capture_from_cpu()`, `restore_to_cpu()`, and serialization.
- Generated authentic ARM64 machine code routines `emit_el0_trap_entry()` and `emit_el0_trap_exit()` restoring full state and executing `ERET`.
- Validated via unit test: `tests/unit/stage211_2_context_preservation_tests.cpp`.

### Stage 211.3 — EL0 Privilege Separation & Exception Vectoring
- Formally verified ARM64 exception vectoring from lower EL (EL0) to kernel EL (EL1) using AArch64.
- Configured `VBAR_EL1` vector base at `0x8000`, setting Lower EL synchronous vector at offset `0x400`.
- Verified execution of `svc #0` transitions PSTATE mode to EL1, writes return address to `ELR_EL1`, sets `ESR_EL1.EC = 0x15`, and returns via `ERET` to EL0.
- Validated via unit test: `tests/unit/stage211_3_el0_privilege_tests.cpp`.

### Stage 211.4 — BSD Process Architecture Research
- Conducted deep source analysis of pinned `xnu-10002.1.13` (`bsd/sys/proc_internal.h`, `bsd/kern/kern_fork.c`, `bsd/kern/kern_exit.c`).
- Documented data structures: `proc`, `ucred`, `task_t`, `thread_t`, and file descriptor tables.
- Produced technical analysis: `docs/campaign211/211_bsd_source_analysis.md`.

### Stages 211.5 & 211.6 — BSD Process Structure & Lifecycle Management
- Implemented `BsdProcess`, `BsdProcessManager`, and `FileDescriptorTable` in `src/kernel/bsd_process.h` and `src/kernel/bsd_process.cpp`.
- Modeled authentic XNU process states: `Starting` (`SIDL`), `Runnable` (`SRUN`), `Sleeping` (`SSLEEP`), `Stopped` (`SSTOP`), and `Zombie` (`SZOMB`).
- Implemented monotonic PID allocation, reserving PID 0 for kernel task and PID 1 for init/launchd.
- Implemented pre-plumbed standard file descriptors (stdin=0, stdout=1, stderr=2).
- Validated via unit test: `tests/unit/stage211_5_bsd_process_tests.cpp`.

### Stage 211.7 — User Address-Space Creation (`UserAddressSpace`)
- Created `UserAddressSpace` in `src/memory/user_address_space.h` and `src/memory/user_address_space.cpp`.
- Backed by isolated Stage-1 translation table at `root_l2_table_pa_` using 16KB granule.
- Implemented boundary-enforced user space limits (`kUserSpaceLimitVa = 0x80000000ULL` / 2 GiB).
- Implemented safe kernel-to-user and user-to-kernel memory copies (`copyin`, `copyout`) with strict validation (`validate_user_buffer`).
- Validated via unit test: `tests/unit/stage211_7_user_address_space_tests.cpp`.

### Stages 211.8, 211.9, 211.10 — Mach-O Userspace Image Loading
- Developed `MachoUserLoader` in `src/loader/macho_user_loader.h` and `src/loader/macho_user_loader.cpp`.
- Implemented validation for `MH_MAGIC_64` (0xFEEDFACF) and `CPU_TYPE_ARM64` (0x0100000C).
- Parsed `LC_SEGMENT_64` commands:
  - `__PAGEZERO`: Enforced unmapped guard page (`VM_PROT_NONE`).
  - `__TEXT`: Mapped as `UserExecute` (AP=0b11, UXN=0, PXN=1).
  - `__DATA_CONST`: Mapped as `UserRead` (AP=0b11, UXN=1, PXN=1).
  - `__DATA`: Mapped as `UserReadWrite` (AP=0b01, UXN=1, PXN=1).
- Enforced strict W^X memory security (rejecting segments marked simultaneously writable and executable).
- Compiled two freestanding ARM64 Mach-O binaries using Zig 0.17.0:
  - `user_first_app.macho` (50,008 bytes, SHA256: `577a677b413c84e5c21c388a8cfa8d18da5aedb39bf14aa7ea95e56d0053d60e`).
  - `user_init_app.macho` (50,008 bytes, SHA256: `29bee7f1de04f619d6bb82b5e8e39be055f68748b3c45cc1075da6956dc94cc9`).
- Embedded binaries into C++ inline header: `src/loader/embedded_user_apps_211.inl`.
- Validated via unit test: `tests/unit/stage211_9_macho_user_loader_tests.cpp`.

### Stages 211.11 & 211.12 — User Stack Construction & Thread Initialization
- Developed `UserStackBuilder` in `src/kernel/user_stack.h` and `src/kernel/user_stack.cpp`.
- Allocated 64 KiB user stack memory in `UserAddressSpace` (`0x7FFE0000` to `0x7FFF0000`).
- Constructed initial stack layout conforming to AAPCS64: string contents pool, NULL-terminated pointer arrays for `envp` and `argv`, and `argc` count.
- Enforced strict 16-byte alignment on user stack pointer (`final_sp_el0`).
- Validated via unit test: `tests/unit/stage211_11_user_stack_tests.cpp`.

### Stages 211.14, 211.15, 211.16 — System Call Dispatcher & Virtual Console
- Implemented `UserSyscallDispatcher` and `VirtualConsole` in `src/kernel/user_syscall_dispatcher.h` and `src/kernel/user_syscall_dispatcher.cpp`.
- Implemented calling convention support:
  - Darwin XNU ABI: `X16` = syscall number, `X0..X5` = arguments, `X0` = return value.
  - WinIOS Fast ABI: `X0` = syscall number.
- Implemented essential Darwin syscalls:
  - `SYS_exit` (1): Terminate process with status code.
  - `SYS_write` (4): Validated write to stdout/stderr or file descriptor with memory buffer checks.
  - `SYS_getpid` (20): Retrieve process PID.
  - `SYS_mach_msg_trap` (31): Mach message trap handler.
  - `SYS_getppid` (39): Retrieve parent process PID.
  - `SYS_sched_yield` (329): Voluntarily yield remaining thread time quantum.
- Validated via unit test: `tests/unit/stage211_15_essential_syscalls_tests.cpp`.

### Stages 211.17 & 211.18 — Mach IPC Expansion & Port Rights
- Added `MachPortRight` and `ProcessIpcSpace` to `src/kernel/ipc.h` and `src/kernel/bsd_process.h`.
- Supported rights: `Receive`, `Send`, `SendOnce`, `PortSet`, `DeadName`.
- Implemented special process ports: `TASK_PORT` (1), `TASK_BOOTSTRAP_PORT` (2), `TASK_HOST_PORT` (3).
- Implemented inter-process port translation and message delivery across distinct process IPC spaces.
- Validated via unit test: `tests/unit/stage211_17_mach_ipc_expansion_tests.cpp`.

### Stage 211.20 — Minimal Init-Like Process (PID 1)
- Launched `user_init_app.macho` under `UserspaceCoordinator`.
- Process executed 17 ARM64 instructions at EL0 and handled 5 distinct syscalls:
  1. `SYS_write`: Output startup banner `"[INIT] WinIOS userland init started (PID 1)\n"`.
  2. `SYS_getpid`: Verified PID returned is 1.
  3. `SYS_sched_yield`: Yielded execution quantum to scheduler.
  4. `SYS_write`: Output completion banner `"[INIT] Init process completed successfully"`.
  5. `SYS_exit`: Exited cleanly with return status 0.
- Validated via unit test: `tests/unit/stage211_20_minimal_init_process_tests.cpp`.

### Stage 211.21 — First Userspace Launch Integration
- Designed and implemented `UserspaceCoordinator` in `src/runtime/userspace_coordinator.h` and `src/runtime/userspace_coordinator.cpp`.
- Orchestrated end-to-end launch: physical page management, user address space mapping, Mach-O parsing, stack construction, thread creation, Stage-1 MMU configuration (TCR_EL1, MAIR_EL1, TTBR0_EL1, SCTLR_EL1), exception vectoring (`VBAR_EL1`), execution stepping, and syscall interception.
- Validated via unit tests: `tests/unit/stage211_13_first_el0_execution_tests.cpp` and `tests/unit/stage211_21_first_userspace_integration_tests.cpp`.

### Stage 211.22 — Adversarial Security Hardening
- Designed and executed an 18-vector security test suite in `tests/unit/stage211_22_adversarial_userspace_security_tests.cpp`.
- Defended against null-pointer dereferences, W^X violations, kernel VA boundary probing, unmapped memory access, sysreg tampering from EL0, malicious Mach-O images, invalid FDs, buffer pointer overflows, and invalid syscall numbers.
- Verified 18/18 vectors passed with zero escapes.

### Stage 211.23 — Deterministic Snapshot & Replay
- Implemented state serialization for `UserAddressSpace`, `XnuVmMap`, `BsdProcess`, `FileDescriptorTable`, and `ProcessIpcSpace`.
- Verified bit-exact serialization and deserialization in `tests/unit/stage211_23_snapshot_replay_tests.cpp`.

### Stage 211.24 — Full Regression Verification
- Integrated all 13 new test suites into `CMakeLists.txt`.
- Executed full project test suite in Release configuration.
- Result: **649 passed, 1 skipped (`inferno_integration_tests`), 0 failed** across 650 registered tests in 90.97 seconds.

---

## 3. EVIDENCE OF GENUINE USERSPACE EXECUTION

### A. First User Application (`user_first_app.macho`)
- **Virtual Memory Address Range:** `0x10000` – `0x11454`
- **Entry Point:** `0x11400`
- **Instructions Executed at EL0:** 12
- **Syscalls Dispatched:** 3 (`SYS_write`, `SYS_getpid`, `SYS_exit`)
- **Captured Console Output:**
  ```text
  Hello from WinIOS EL0 Userspace!
  ```
- **Execution Step Trace:**
  ```text
  step 0: pc=0x11400 insn=0xD2800540 (mov x0, #42)               el=0
  step 1: pc=0x11404 insn=0x91002000 (add x0, x0, #8)            el=0
  step 2: pc=0x11408 insn=0xD2800090 (mov x16, #4: SYS_write)    el=0
  step 3: pc=0x1140C insn=0xD2800020 (mov x0, #1: stdout)        el=0
  step 4: pc=0x11410 insn=0x10000101 (adr x1, 0x11430 message)   el=0
  step 5: pc=0x11414 insn=0xD2800442 (mov x2, #34: length)       el=0
  step 6: pc=0x11418 insn=0xD4000001 (svc #0) -> SYS_write ret=34 el=0 -> el=1
  step 7: pc=0x1141C insn=0xD2800290 (mov x16, #20: SYS_getpid)  el=0
  step 8: pc=0x11420 insn=0xD4000001 (svc #0) -> SYS_getpid ret=1 el=0 -> el=1
  step 9: pc=0x11424 insn=0xD2800030 (mov x16, #1: SYS_exit)    el=0
  step 10: pc=0x11428 insn=0xD2800000 (mov x0, #0)               el=0
  step 11: pc=0x1142C insn=0xD4000001 (svc #0) -> SYS_exit ret=0 el=0 -> el=1
  ```

### B. Minimal Init Process (`user_init_app.macho`)
- **Virtual Memory Address Range:** `0x10000` – `0x11490`
- **Entry Point:** `0x11400`
- **Process PID:** 1 (Root Userland Init)
- **Instructions Executed at EL0:** 17
- **Syscalls Dispatched:** 5 (`SYS_write`, `SYS_getpid`, `SYS_sched_yield`, `SYS_write`, `SYS_exit`)
- **Captured Console Output:**
  ```text
  [INIT] WinIOS userland init started (PID 1)
  [INIT] Init process completed successfully
  ```

---

## 4. TEST VERIFICATION SUMMARY

| Stage | Test Suite Executable | Test Cases | Execution Time | Result |
|:---|:---|:---:|:---:|:---:|
| 211.2 | `stage211_2_context_preservation_tests.exe` | 8 | 0.01s | **PASSED** |
| 211.3 | `stage211_3_el0_privilege_tests.exe` | 9 | 0.01s | **PASSED** |
| 211.5 | `stage211_5_bsd_process_tests.exe` | 14 | 0.01s | **PASSED** |
| 211.7 | `stage211_7_user_address_space_tests.exe` | 10 | 0.02s | **PASSED** |
| 211.9 | `stage211_9_macho_user_loader_tests.exe` | 8 | 0.01s | **PASSED** |
| 211.11 | `stage211_11_user_stack_tests.exe` | 9 | 0.01s | **PASSED** |
| 211.13 | `stage211_13_first_el0_execution_tests.exe` | 7 | 0.01s | **PASSED** |
| 211.15 | `stage211_15_essential_syscalls_tests.exe` | 12 | 0.02s | **PASSED** |
| 211.17 | `stage211_17_mach_ipc_expansion_tests.exe` | 10 | 0.01s | **PASSED** |
| 211.20 | `stage211_20_minimal_init_process_tests.exe` | 7 | 0.04s | **PASSED** |
| 211.21 | `stage211_21_first_userspace_integration_tests.exe` | 6 | 0.02s | **PASSED** |
| 211.22 | `stage211_22_adversarial_userspace_security_tests.exe` | 18 | 0.01s | **PASSED** |
| 211.23 | `stage211_23_snapshot_replay_tests.exe` | 11 | 0.01s | **PASSED** |
| **All** | **Stage 211 Test Suites Combined** | **129** | **0.28s** | **100% PASSED** |
| **Project** | **Full Regression Suite (Stage 211.24)** | **650** | **90.97s** | **649 PASSED, 1 SKIPPED, 0 FAILED** |

---

## 5. GIT & ASSET INTEGRITY AUDIT

- **Active Branch:** `campaign-191r`
- **Protected File Checksum Verification:**
  - `D:\WinISO26\WinIOS_README.md`: `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` (VERIFIED UNMODIFIED)
  - `D:\WinISO26\WinIOS\docs\progress.md`: `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` (VERIFIED UNMODIFIED)
  - `D:\WinISO26\Inferno-Research/`: VERIFIED UNTOUCHED
- **Compiler Compliance:** MSVC C++20 with `/W4 /WX /permissive-` (zero warnings, zero errors).
- **Push Policy:** Strictly local commits only on `campaign-191r` — **DO NOT PUSH**.

---

## 6. CONCLUSION & READINESS FOR CAMPAIGN 212

Campaign 211 has fulfilled all primary and stretch requirements set forth in the master engineering prompt:
1. Complete ARM64 context preservation and trap frame architecture.
2. Full BSD process lifecycle and file descriptor table plumbing.
3. Isolated Stage-1 16KB user address space with memory bounds safety.
4. Freestanding 64-bit Mach-O loader with W^X enforcement.
5. AAPCS64 user stack construction.
6. Genuine EL0 instruction execution with synchronous `svc #0` vectoring to EL1.
7. Essential Darwin system calls with observable console output.
8. Execution of a minimal `launchd`-like init process (PID 1).
9. Complete 18-vector adversarial security hardening.
10. Bit-exact snapshot and replay capability.

WinIOS now possesses a fully verified and functional EL0 userspace Mach-O application execution environment, ready for Campaign 212.
