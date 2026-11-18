# WINIOS CAMPAIGN 220 — STAGE 220.11 PROCESS LIFECYCLE & ISOLATION REPORT

## 1. Architectural Overview

WinIOS models multi-process execution according to pinned Darwin/XNU semantics (`xnu-10002.1.13 bsd/kern/kern_fork.c`, `bsd/kern/kern_exit.c`). Each guest process is encapsulated in a `BsdProcess` object managed by `BsdProcessManager`, maintaining strict structural boundaries between processes.

```
                      +-----------------------------+
                      |   PID 0: kernel_task        |
                      +-----------------------------+
                                     |
                                     v
                      +-----------------------------+
                      |   PID 1: launchd (Init)     |
                      +-----------------------------+
                            /                 \
                           v                   v
              +----------------------+   +----------------------+
              | Process A (PID 2)    |   | Process B (PID 5)    |
              | ASID: 10             |   | ASID: 20             |
              | FD Table: [0..63]    |   | FD Table: [0..63]    |
              | UAS: root_pa_a       |   | UAS: root_pa_b       |
              +----------------------+   +----------------------+
                     /           \
                    v             v
         +--------------+   +--------------+
         | Child 1 (P3) |   | Child 2 (P4) |
         +--------------+   +--------------+
```

---

## 2. Key Lifecycle Mechanisms Verified

### 2.1 PID Allocation & Process Hierarchy
- PID 0 is reserved for the Mach kernel task (`kernel_task`), initialized in `BsdProcessManager::initialize()`.
- User processes receive monotonically increasing PIDs in `[1, 32767]` with collision wrap-around avoidance.
- When a process creates a child, the parent's `children_` list records the child PID, and the child's `ppid_` points to the parent.

### 2.2 Orphan Reparenting (XNU Alignment)
- In conformance with XNU `bsd/kern/kern_exit.c`:
  When a parent process exits (e.g. PID 2 terminates), any active or zombie children (PIDs 3 and 4) are immediately reparented to PID 1 (`launchd`).
  ```cpp
  BsdProcess* init_proc = get_process(1);
  for (pid_t child_pid : proc->children()) {
      BsdProcess* child = get_process(child_pid);
      if (child) {
          child->set_ppid(1);
          if (init_proc && init_proc->pid() != child_pid) {
              init_proc->add_child(child_pid);
          }
      }
  }
  proc->clear_children();
  ```
- This prevents orphaned processes from entering un-reapable states.

### 2.3 Zombie Reaping & SYS_wait4 Semantics
- Terminated processes enter `ProcessState::Zombie`.
- `BsdProcessManager::wait_process(parent_pid, wpid, out_status, options)` implements POSIX / Darwin `wait4`:
  - `wpid > 0`: Reaps specified child if terminated, formats exit status (`(exit_code & 0xFF) << 8`), unlinks from parent, and reclaims process descriptor.
  - `wpid == -1`: Reaps any terminated child of caller.
  - Returns `-ECHILD` (-10) if caller has no children or if target `wpid` is not a child.
  - Supports `WNOHANG` non-blocking polling.

### 2.4 Resource & Memory Isolation
- **Address Space Isolation:** Each `BsdProcess` owns an independent `UserAddressSpace` with a dedicated root page table PA, independent ASID, and private page allocations.
- **Descriptor Table Isolation:** Each process owns a private `FileDescriptorTable` (up to 64 descriptors). Closing or corrupting a descriptor in Process A has zero impact on Process B, even if both processes allocate identical descriptor numbers (e.g., fd 0, 1, 2).
- **Automatic Cleanup on Exit:** When a process terminates, all associated threads are set to `ThreadState::Terminated` and all open file descriptors are closed automatically.
