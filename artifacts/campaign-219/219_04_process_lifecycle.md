# WinIOS Campaign 219 — Stage 219.4: Process Lifecycle Management

## 1. Process Creation & Hierarchy

Process hierarchy in WinIOS follows strict Darwin/POSIX semantics:
- Root process `kernel_task` occupies PID 0.
- First userspace process `launchd` occupies PID 1 (`ppid = 0`).
- User processes are spawned with explicit parent PID linkage (`ppid`). The parent maintains a list of child PIDs in `children_`.

## 2. Process Termination & Zombie State

When a process invokes `SYS_exit(code)`:
1. State is immediately transitioned to `ProcessState::Zombie`.
2. Exit code is saved in `exit_code_`.
3. Open file descriptors in `fd_table_` are closed, freeing any active pipe readers/writers and VFS references.
4. Active threads attached to the process are marked `Terminated`.
5. The process's children (if any) are immediately **reparented to PID 1** (`launchd`), conforming to Darwin `bsd/kern/kern_exit.c`.
6. Parent process is notified (`notify_parent()`).

## 3. Reaping & Wait4 Semantics

Parent processes collect terminated child statuses using `BsdProcessManager::wait_process` (implementing `SYS_wait4` semantics):
- When `wpid > 0`, the manager verifies that `wpid` is a registered child of the caller.
- If the child is terminated (`Zombie`), its exit status is packed into `*out_status = (exit_code & 0xFF) << 8`.
- The child process is removed from the parent's child list, and its entry in `processes_` is purged (`reap_process`).
- If `wpid == -1`, any terminated child of the calling process is reaped.
- If no children are terminated and `WNOHANG` is set, `wait_process` returns 0 without blocking. If no children exist, `-ECHILD` (-10) is returned.

## 4. Verification Evidence

Validated via `stage219_3_address_space_isolation_tests.exe`:
- Spawning child process PID 3 under parent PID 2.
- Allocation of child file descriptor `/tmp/child.log`.
- Child termination with exit status 123.
- Automatic descriptor cleanup on termination.
- Parent `wait_process` returning child PID 3 with packed status `(123 << 8)`.
- Reaping confirmed by verifying `get_process(3) == nullptr`.
- Orphan reparenting verified: terminating parent PID 2 reassigns grandchild PID 4's PPID to PID 1.
