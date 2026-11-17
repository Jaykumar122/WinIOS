# WINIOS CAMPAIGN 211: STAGE 211.4 BSD PROCESS ARCHITECTURE RESEARCH

**Campaign:** 211  
**Stage:** 211.4 — BSD Process Architecture and XNU Source Analysis  
**Pinned Public XNU Source:** Apple `xnu-10002.1.13` (macOS 14 Sonoma / iOS 17 Open Source, APSL-2.0)  
**Date:** 2026-11-17  

---

## 1. Executive Summary

In Apple's Darwin / XNU operating system architecture, the process abstraction is split across two core layers:
1. **Mach Core (Microkernel Layer):**
   - **`task_t` (`osfmk/kern/task.h`):** The primary resource container. Owns the virtual address space (`vm_map_t`), port rights table (`ipc_space_t`), memory limits, and the collection of Mach threads (`thread_t`). Mach tasks have no concept of POSIX process IDs, signals, or file descriptors.
   - **`thread_t` (`osfmk/kern/thread.h`):** The schedulable execution entity. Maintains execution context, saved architectural registers (`arm_saved_state_t`), priority, and stack.
2. **BSD Layer (POSIX & System Services Layer):**
   - **`struct proc` (`bsd/sys/proc_internal.h`):** Wraps a Mach `task_t` with POSIX semantics, including process IDs (`p_pid`), parent process IDs (`p_ppid`), process group, credentials (`p_ucred`), file descriptor table (`struct filedesc`), signal handling, and exit status.

---

## 2. Key XNU Structures & Dependency Ordering

### 2.1 `struct proc` Layout & Semantics
From `xnu-10002.1.13/bsd/sys/proc_internal.h`:
```c
struct proc {
    LIST_ENTRY(proc) p_list;           /* [0x00] Global process list */
    pid_t            p_pid;            /* [0x10] Process identifier */
    void *           task;             /* [0x18] Associated Mach task_t */
    struct proc *    p_pptr;           /* [0x20] Pointer to parent process */
    pid_t            p_ppid;           /* [0x28] Parent PID */
    pid_t            p_pgrpid;         /* [0x2C] Process group ID */
    kauth_cred_t     p_ucred;          /* [0x30] Process credentials */
    struct filedesc *p_fd;             /* [0x38] File descriptor table */
    char             p_stat;           /* [0x40] Process status */
    char             p_shutdownstate;  /* [0x41] Shutdown status */
    short            p_xstat;          /* [0x42] Exit status for wait */
    char             p_comm[MAXCOMLEN+1]; /* [0x44] Process name */
};
```

### 2.2 Process Lifecycle States (`p_stat`)
From `xnu-10002.1.13/bsd/sys/proc.h`:
- `SIDL = 1`: Process being created (fork/spawn in progress).
- `SRUN = 2`: Process runnable / active on CPU.
- `SSLEEP = 3`: Process waiting on a resource/event.
- `SSTOP = 4`: Process stopped (by signal or debugger).
- `SZOMB = 5`: Process exited, awaiting parent `wait4()`.

### 2.3 BSD Subsystem Bootstrap (`bsd_init`)
From `xnu-10002.1.13/bsd/kern/bsd_init.c`:
1. `bsd_init()` is called from Mach `machine_startup()` after Mach VM, IPC, and task/thread subsystems are online.
2. `bsd_init()` establishes:
   - `kernproc` (PID 0): Attached directly to `kernel_task`. Owns kernel file descriptors and credentials.
   - `initproc` (PID 1): The root user process (historically `/sbin/mach_init` or `/sbin/launchd`).
3. For launching userland binaries, XNU uses `posix_spawn()` / `execve()`:
   - Evaluates the Mach-O binary header (`mach_header_64`).
   - Replaces/initializes the user `vm_map`.
   - Sets up initial stack with arguments (`argc`, `argv`, `envp`, and Apple-specific exec vector).
   - Sets thread registers (`PC = entry_point`, `SP = user_stack`).

---

## 3. WinIOS Compatibility Mapping & Strategy

For Campaign 211, WinIOS implements a clean C++20 object model mapping these XNU abstractions:
- **`BsdProcess`:** Models `struct proc`, holding `pid`, `ppid`, `state`, pointer to `KernelTask`, pointer to `UserAddressSpace`, and `FileDescriptorTable`.
- **`BsdProcessManager`:** Implements process lifecycle (`create_process`, `fork`, `exec`, `terminate`, `reap`).
- **PID 0 (`kernel_process`):** Created during bootstrap, associated with PID 0 `kernel_task`.
- **PID 1 (`user_init_process`):** The first userspace process executing genuine Mach-O code.

This architecture preserves exact structural parity with XNU without taking dependencies on private or uncompilable Apple sources.
