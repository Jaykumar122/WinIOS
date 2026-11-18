# WinIOS Campaign 219 — Stage 219.2: Multi-Process Isolation Architecture Specification & Process State Model

## 1. Architectural Overview

WinIOS Campaign 219 establishes genuine guest multi-process execution atop a Windows 11 x64 host. Each guest process operates as an autonomous security and memory domain, isolating user virtual memory, thread execution contexts, open file descriptors, and IPC capabilities.

```
+-----------------------------------------------------------------------+
|                             WinIOS Kernel                             |
|                                                                       |
|  +--------------------+                     +--------------------+   |
|  |   BsdProcess 1     |                     |   BsdProcess 2     |   |
|  |  (PID 1, launchd)  |                     |  (PID 2, worker)   |   |
|  |                    |                     |                    |   |
|  |  +--------------+  |                     |  +--------------+  |   |
|  |  | UserAddress  |  |                     |  | UserAddress  |  |   |
|  |  | Space (L2-L3)|  |                     |  | Space (L2-L3)|  |   |
|  |  | TTBR0_EL1: A |  |                     |  | TTBR0_EL1: B |  |   |
|  |  +--------------+  |                     |  +--------------+  |   |
|  |  | FD Table (64)|  |      Pipe IPC       |  | FD Table (64)|  |   |
|  |  | 0, 1, 2, ... |==|====================>|  | 0, 1, 2, ... |  |   |
|  |  +--------------+  |  (circular ring buf)|  +--------------+  |   |
|  |  | Threads (1..N|  |                     |  | Threads (1..N|  |   |
|  |  +--------------+  |                     |  +--------------+  |   |
|  +--------------------+                     +--------------------+   |
|             \                                     /                  |
|              \---> [ MultiProcessScheduler ] <---/                   |
|                               |                                      |
|                               v                                      |
|                     [ Virtual ARM64 CPU ]                            |
+-----------------------------------------------------------------------+
```

## 2. Process State Machine

The guest process lifecycle follows standard BSD/Darwin process semantics (`xnu-10002.1.13` `bsd/sys/proc.h`):

```
       +--------------+
       |   Starting   | (Process being initialized, address space allocated)
       +-------+------+
               |
               v
       +--------------+ <====================+
       |   Runnable   |                      |
       +-------+------+                      |
          |         |                        |
          |         v                        |
          |    +--------------+              |
          |    |   Sleeping   | (Wait on IO) |
          |    +-------+------+              |
          |            +---------------------+
          |
          v
       +--------------+
       |    Zombie    | (Process terminated, exit status retained)
       +-------+------+
               |
               v (Parent reaps via SYS_wait4)
          [ Destroyed ]
```

1. **`Starting` (1)**: The process descriptor is allocated, PID assigned, and parent linkage registered. Address space structures are instantiated.
2. **`Runnable` (2)**: Active execution state. At least one thread is ready to schedule or executing on the virtual CPU.
3. **`Sleeping` (3)**: The process is blocked waiting for an I/O event (e.g. pipe read, message queue arrival, timer expiry).
4. **`Stopped` (4)**: Suspended execution.
5. **`Zombie` (5)**: Process has invoked `SYS_exit` or received fatal termination. Execution context is halted, file descriptors are closed, and exit status is held until reaped by parent via `SYS_wait4`.

## 3. Resource Bounds & Security Invariants

- **PID Namespace**: 16-bit integer space (PIDs 0 through 32767). PID 0 is permanently reserved for `kernel_task`. PID 1 is initialized as root userspace supervisor (`launchd`).
- **File Descriptors**: Bounded at exactly 64 file descriptors per process (`FileDescriptorTable::kMaxFiles = 64`). Descriptors 0, 1, and 2 default to `/dev/console` (stdin, stdout, stderr).
- **Virtual Address Space Limit**: 32 GiB user address space limit (`0x0000000000000000` to `0x0000000800000000`). Addresses beyond 32 GiB are strictly disallowed in EL0.
- **Orphan Reparenting**: When any parent process terminates, all active child processes are immediately reparented to PID 1 (`launchd`), preventing orphaned zombie leaks.
