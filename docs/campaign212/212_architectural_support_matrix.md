# WinIOS Campaign 212: Darwin Userspace Architectural Support Matrix

**Target:** Apple A16 Bionic (`T8120`, `iPhone15,4`, `D37AP`)  
**Host:** Windows 11 x64 / MSVC C++20 (`/W4 /WX /permissive-`)  
**Source Baseline:** `xnu-10002.1.13` (Pinned Public Darwin/XNU)  

---

## 1. BSD System Call Subsystem

| Syscall # | Syscall Name | ABI Conformance | Registers (In / Out) | Error Reporting (Darwin Strict) | Implementation Status |
|---|---|---|---|---|---|
| 1 | `SYS_exit` | DarwinStrict / WinIOSFast | `X0`=exit code | Process terminated, threads reaped | **Verified & Operational** |
| 3 | `SYS_read` | DarwinStrict / WinIOSFast | `X0`=fd, `X1`=buf, `X2`=nbytes / `X0`=bytes | `PSTATE.C`=1, `X0`=errno (`EBADF`, `EFAULT`) | **Verified & Operational** |
| 4 | `SYS_write` | DarwinStrict / WinIOSFast | `X0`=fd, `X1`=buf, `X2`=nbytes / `X0`=bytes | `PSTATE.C`=1, `X0`=errno (`EBADF`, `EFAULT`, `EPIPE`) | **Verified & Operational** |
| 5 | `SYS_open` | DarwinStrict / WinIOSFast | `X0`=path, `X1`=flags, `X2`=mode / `X0`=fd | `PSTATE.C`=1, `X0`=errno (`ENOENT`, `EFAULT`, `EMFILE`) | **Verified & Operational** |
| 6 | `SYS_close` | DarwinStrict / WinIOSFast | `X0`=fd / `X0`=0 | `PSTATE.C`=1, `X0`=errno (`EBADF`) | **Verified & Operational** |
| 7 | `SYS_wait4` | DarwinStrict / WinIOSFast | `X0`=pid, `X1`=status_va, `X2`=options / `X0`=reaped_pid | `PSTATE.C`=1, `X0`=errno (`ECHILD`, `EFAULT`) | **Verified & Operational** |
| 20 | `SYS_getpid` | DarwinStrict / WinIOSFast | None / `X0`=pid | Always succeeds | **Verified & Operational** |
| 39 | `SYS_getppid` | DarwinStrict / WinIOSFast | None / `X0`=ppid | Always succeeds | **Verified & Operational** |
| 42 | `SYS_pipe` | DarwinStrict / WinIOSFast | `X0`=fd_array_va / `X0`=read_fd, `X1`=write_fd | `PSTATE.C`=1, `X0`=errno (`EMFILE`, `EFAULT`) | **Verified & Operational** |
| 199 | `SYS_lseek` | DarwinStrict / WinIOSFast | `X0`=fd, `X1`=offset, `X2`=whence / `X0`=offset | `PSTATE.C`=1, `X0`=errno (`EBADF`, `ESPIPE`, `EINVAL`) | **Verified & Operational** |
| 329 | `SYS_sched_yield` | DarwinStrict / WinIOSFast | None / `X0`=0 | Triggers preemption / voluntary yield | **Verified & Operational** |
| 338 | `SYS_fstat64` | DarwinStrict / WinIOSFast | `X0`=fd, `X1`=stat_va / `X0`=0 | `PSTATE.C`=1, `X0`=errno (`EBADF`, `EFAULT`) | **Verified & Operational** |
| 339 | `SYS_stat64` | DarwinStrict / WinIOSFast | `X0`=path, `X1`=stat_va / `X0`=0 | `PSTATE.C`=1, `X0`=errno (`ENOENT`, `EFAULT`) | **Verified & Operational** |

---

## 2. Process Management & Hierarchy

| Feature | Design Specification | Implementation | Conformance Status |
|---|---|---|---|
| **PID 0** | Darwin Kernel Process (`kernel_task`) | Instantiated during `BsdProcessManager::initialize()` | Fully Compliant |
| **PID 1** | Darwin Init Process (`launchd` / `InitCoordinator`) | Root supervisor; reaps all orphaned children | Fully Compliant |
| **PIDs >= 2** | User Application and Service Processes | Spawned via `ProcessSpawner` or `InitCoordinator` | Fully Compliant |
| **Child Tracking** | `std::vector<pid_t> children_` per process | Tracked on creation; removed on reap | Fully Compliant |
| **Orphan Adoption** | Automatic reparenting to PID 1 upon parent termination | Implemented in `BsdProcessManager::terminate_process` | Fully Compliant |
| **Zombie Reaping** | `SYS_wait4` with WNOHANG or synchronous wait | Reaps terminated child, returns exit status in bits 15:8 | Fully Compliant |

---

## 3. Dynamic Linking Foundations (dyld)

| Mach-O Load Command | Purpose | WinIOS Guest Dynamic Loader Handling |
|---|---|---|
| `LC_LOAD_DYLIB` | Standard shared library dependency | Parsed, topologically sorted via DAG, loaded into non-overlapping VA |
| `LC_LOAD_WEAK_DYLIB` | Weak optional dependency | Parsed; ignored if absent without aborting executable startup |
| `LC_ID_DYLIB` | Dynamic library identification name | Validated for identity matching during dylib registration |
| `LC_SEGMENT_64` | 64-bit segment mapping | Mapped into guest virtual memory enforcing strict W^X permissions |
| GOT Indirection | Indirect symbol resolution | Relocation records parsed; GOT slots filled with resolved symbols |

---

## 4. Virtual File System (VFS)

| Path / Feature | Type | Behavior / Capabilities |
|---|---|---|
| `/dev/null` | Character Device | Reads return EOF (0 bytes); writes succeed silently |
| `/dev/zero` | Character Device | Reads return zero bytes; writes succeed silently |
| `/dev/random` & `/dev/urandom` | Character Device | Reads return pseudorandom byte stream |
| `/dev/console` | Character Device | Writes captured to `VirtualConsole` stdout/stderr buffers |
| `/tmp` | Directory | Supports file creation, writing, reading, and unlinking |
| `/etc/hosts` | Regular File | Initialized with standard localhost loopback mappings |
| Symlinks | Symbolic Link | Path traversal with cycle detection (ELOOP) up to 32 hops |
| Path Normalization | Static Algorithm | Resolves `.`, `..`, multiple slashes `/a/b/../../c` -> `/c` |

---

## 5. Multi-Process Scheduling & Context Switching

| Component | Architecture Specification | Realized Behavior |
|---|---|---|
| **Scheduling Algorithm** | Round-robin with 4-tick time slice quantum | Enforces fair CPU share across ready processes |
| **Address Space Switch** | `TTBR0_EL1` update and TLB invalidation | Switched cleanly in `perform_context_switch` without cross-leakage |
| **Register State Preservation** | Complete AAPCS64 context (GPRs 0-30, SP_EL0, PC, SPSR_EL1) | Preserved in Thread Control Block (`tcb_`) on switch |
| **Preemption** | Triggered on quantum expiration in `scheduler.tick(cpu)` | Preempts current thread, moves to ready queue, switches to next |
| **Voluntary Yield** | Triggered by `SYS_sched_yield` (329) | Yields CPU immediately, recharges quantum |
