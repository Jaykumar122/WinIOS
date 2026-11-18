# WinIOS Application Process API Report

**Campaign:** 221 (Stage 221.7)  
**Subsystem:** Process Spawning, Supervision & Status Tracking  

---

## 1. Developer Process API

Exposed through `<winios/system/process.h>`:
- `winios_spawn_process(exec_path, argv, out_pid)`: Spawns a child process under the caller, allocating a new PID and inheriting sandbox boundaries.
- `winios_waitpid(pid, out_status)`: Blocks or queries for child termination and reaps exit code.
- `winios_get_process_info(pid, out_info)`: Retrieves PID, PPID, execution state, retired instructions, and heap usage.
- `winios_terminate_process(pid, code)`: Sends termination signal to a child process.

---

## 2. Kernel Backing & Security Restrictions

1. **Hierarchy Constraints:** Applications can only query or terminate processes within their own process tree (descendants) or if granted `ProcessSpawn` capability.
2. **Reaping Mechanics:** Follows standard BSD `SYS_wait4` conventions. Reaped processes are purged from `BsdProcessManager` and their ASID resources freed.
