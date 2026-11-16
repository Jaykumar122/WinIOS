# Stage 204.14: BSD Syscall Dispatcher Expansion Verification Report

## 1. Overview
Stage 204.14 unifies and expands the BSD syscall dispatching architecture across `DarwinSyscallDispatcher` and `DarwinBsdSubsystem` for Darwin ARM64 `SVC #0x80`. It strictly conforms to the Apple Darwin ARM64 kernel ABI:
- Syscall number passed in register `X16`.
- Arguments passed in registers `X0..X5`.
- **On Success**: Return value placed in `X0`, Carry flag `C` (bit 1 of `NZCV`) is **CLEARED** (`C = 0`).
- **On Error**: Positive POSIX `errno` placed in `X0`, Carry flag `C` (bit 1 of `NZCV`) is **SET** (`C = 1`).

## 2. Key Implementations
1. **Dispatcher Architecture & Subsystem Binding:**
   - Added `bind_bsd_subsystem(std::shared_ptr<DarwinBsdSubsystem> bsd, std::uint32_t pid)` to `DarwinSyscallDispatcher`.
   - Connected `DarwinSyscallDispatcher` to the underlying VFS and Process table, delegating operations cleanly while supporting standalone mock operation.

2. **Syscall Coverage & Conformance:**
   - `SYS_exit` (1): Captures guest exit code, marks process terminated.
   - `SYS_read` (3): Reads from descriptors into memory; returns `-9` (`-EBADF`) on invalid descriptor.
   - `SYS_write` (4): Writes from memory to stdout/stderr and file descriptors; returns `-9` (`-EBADF`) on write error.
   - `SYS_open` (5): Reads path string from memory, invokes VFS open; returns valid fd or `-2` (`-ENOENT`).
   - `SYS_close` (6): Closes file descriptor; returns 0 or `-9` (`-EBADF`).
   - `SYS_getpid` (20): Returns bound process ID.
   - `SYS_getuid` (24), `SYS_geteuid` (25), `SYS_getgid` (47), `SYS_getegid` (43): Returns process credential IDs.
   - `SYS_dup` (41) & `SYS_dup2` (90): Clones descriptor references.
   - `SYS_pipe` (42): Allocates paired unidirectional pipe descriptors in registers `X0` (read) and `X1` (write).
   - `SYS_munmap` (73): Unmaps memory region; validates alignment and bounds.
   - `SYS_mprotect` (74): Enforces W^X page permissions (prohibiting simultaneous `PROT_WRITE` and `PROT_EXEC` unless `MAP_JIT`).
   - `SYS_mmap` (197): Allocates anonymous or file-backed 16KB-aligned memory regions.
   - `SYS_lseek` (199): Calculates new offset according to `SEEK_SET`, `SEEK_CUR`, `SEEK_END`; strictly returns `-22` (`-EINVAL`) or `-ESPIPE` for invalid offsets or pipes.
   - `SYS_stat64` (338) & `SYS_fstat64` (339): Serializes `DarwinStat64Raw` directly into guest memory at specified VA.
   - `SYS_fcntl` (92): Controls descriptor flags (`F_DUPFD`, `F_GETFD`, `F_SETFD`).

3. **ARM64 Register Convention Verification:**
   - Verified that on error, `X0` receives the positive errno (e.g. `2` for `ENOENT`, `9` for `EBADF`, `13` for `EACCES`, `22` for `EINVAL`, `78` for `ENOSYS`), and Carry bit in `NZCV` (bit 1, mask `0x2`) is set.
   - Verified that on success, `X0` holds the return value and Carry bit in `NZCV` is cleared.

## 3. Test Verification
- Target: `stage204_14_bsd_syscall_dispatcher_tests`
- Release build & test: PASS (4/4 test suites, 0 failures)
- Debug build & test: PASS (4/4 test suites, 0 failures)
- Full regression suite maintained across prior stages.
