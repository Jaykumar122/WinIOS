# WinIOS Campaign 219 — Stage 219.14: Pipe Syscall Interface Report

## 1. Syscall Interfaces

The pipe subsystem is surfaced to guest ARM64 applications through four pinned Darwin/XNU system calls:

- **`SYS_pipe` (SYS 42)**:
  - Allocates a reader descriptor and writer descriptor referencing a shared `Pipe` instance.
  - If a user memory buffer is provided in register `X0`, copies out `int fds[2] = {read_fd, write_fd}`.
  - In Darwin Strict ABI, returns `read_fd` in register `X0` and `write_fd` in register `X1`.
- **`SYS_read` (SYS 3)**:
  - Reads bytes from `read_fd` into a validated user buffer `buf_va`.
- **`SYS_write` (SYS 4)**:
  - Writes bytes from validated user buffer `buf_va` into `write_fd`.
- **`SYS_close` (SYS 6)**:
  - Closes either read or write endpoint, notifying the counterpart.

## 2. Test Verification

Verified by `stage219_14_pipe_ipc_tests.exe`:
- Creation of pipe returning valid descriptors.
- Write of 32-byte payload via `SYS_write`.
- Read of exact payload via `SYS_read`.
- Writer closure triggering EOF (0 bytes) on subsequent read.
