# Stage 204.13: File Descriptors & Basic I/O Verification Report

## 1. Overview
Stage 204.13 completes the file descriptor subsystem and basic I/O semantics for the WinIOS Darwin userspace runtime. It implements comprehensive character device support (`/dev/null`, `/dev/zero`, `/dev/urandom`), standard descriptor inheritance (0, 1, 2), non-modifying cursor reads/writes (`pread`, `pwrite`), positioning (`lseek`) with strict error reporting (invalid whence, negative offset, pipe rejection via `ESPIPE`), unidirectional pipe IPC channels with broken-pipe detection (`EPIPE`), file descriptor duplication (`dup`, `dup2`), and file control flags (`fcntl` with `F_DUPFD`, `F_GETFD`, `F_SETFD`, `FD_CLOEXEC`, `F_GETFL`, `F_SETFL`).

## 2. Key Implementations
1. **Character Device Emulation:**
   - `/dev/null`: Read returns EOF (0), write discards data and returns byte count, `lseek` returns 0.
   - `/dev/zero`: Read fills buffer with null bytes (`0x00`) and returns count, write discards data, `lseek` returns 0.
   - `/dev/random` and `/dev/urandom`: Read generates deterministic high-entropy pseudo-random bytes via 64-bit xorshift generator, write discards data.
   - `fstat64` & `stat64` for character devices report `S_IFCHR` (`0020000`).

2. **File Descriptors Lifecycle & Offsets:**
   - Standard streams (0, 1, 2) pre-initialized to `/dev/null` for newly created processes.
   - `pread` and `pwrite` execute I/O at caller-specified offsets without altering `desc.offset`.
   - `lseek` validates bounds and flags: `SEEK_SET`, `SEEK_CUR`, `SEEK_END`, rejecting negative offsets and invalid whence.

3. **Pipe IPC Subsystem:**
   - `pipe()` allocates paired reader and writer descriptors with a shared thread-safe `PipeChannel`.
   - Reading from write-end and writing to read-end are strictly prohibited (-1).
   - `lseek` on pipe descriptors is prohibited with `ESPIPE` (-1).
   - Closing reader causes subsequent writes to return broken-pipe error (`EPIPE` / -1).
   - Closing writer drains remaining buffered bytes then returns EOF (0) to readers.
   - Multi-descriptor sharing (`dup` / `dup2`): pipe channel endpoints only close when all referencing descriptors are closed.
   - `fstat64` on pipe descriptors reports `S_IFIFO` (`0010000`) and reports buffer queue length.

4. **Descriptor Duplication & Control:**
   - `dup` allocates the next lowest available descriptor >= 3.
   - `dup2` atomically closes and replaces the target descriptor or no-ops if oldfd == newfd.
   - `fcntl` supports `F_DUPFD` (allocating >= min_fd), `F_GETFD` / `F_SETFD` for `FD_CLOEXEC`, and `F_GETFL` / `F_SETFL` for runtime flags (`O_NONBLOCK`, `O_APPEND`).

## 3. Test Verification
- Target: `stage204_13_file_descriptors_io_tests`
- Release build & test: PASS (5/5 test suites, 0 failures)
- Debug build & test: PASS (5/5 test suites, 0 failures)
- Compiler: MSVC C++20 (`/W4 /WX /permissive-`)
