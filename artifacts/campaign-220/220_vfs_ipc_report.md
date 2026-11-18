# WINIOS CAMPAIGN 220 — STAGE 220.15 VFS DESCRIPTORS & PIPE IPC REPORT

## 1. Executive Summary

This report covers the architecture, lifecycle, and flow control mechanics of WinIOS's Virtual File System (VFS) descriptor table and bidirectional memory pipe IPC subsystem. All implementations have been validated against standard Darwin/POSIX semantics (`xnu-10002.1.13`).

---

## 2. File Descriptor Architecture & Lifetime

### 2.1 Per-Process Table Isolation
Each `BsdProcess` encapsulates an independent `FileDescriptorTable` (`kMaxFiles = 64`):
- Slots 0, 1, 2 are pre-allocated upon initialization to `stdin`, `stdout`, and `stderr` (console descriptors).
- Dynamic descriptor allocation (`allocate_fd`) selects the lowest available descriptor slot in `[0..63]`.
- Table exhaustion returns `-1` (propagated as POSIX `EMFILE = 24`).

### 2.2 Clone-on-Fork Semantics
When a process forks:
- The child process duplicates the parent's descriptor table via `FileDescriptorTable::clone_from`.
- File descriptors pointing to shared objects (such as `Pipe` or `VfsDescriptor`) hold `std::shared_ptr` references.
- Closing a descriptor in the parent decrements the reference count without disrupting the child's descriptor access.

---

## 3. Pipe IPC Architecture & Flow Control

### 3.1 16 KB Circular Ring Buffer
To match the iPhone 15 A16 Bionic 16 KB translation granule (`0x4000`), the `Pipe` implementation uses a thread-safe circular ring buffer with configurable capacity (defaulting to 16 KB or 64 KB).
```
       head (write ptr)
             |
             v
   [ ... | D5 | D6 | D7 | ..free.. | D0 | D1 | D2 | D3 | D4 | ... ]
                          ^
                          |
                   tail (read ptr)
```
- Wrap-around is calculated modulo buffer capacity (`head = (head + 1) % capacity`).
- FIFO byte ordering is strictly preserved across arbitrary chunk sizes.

### 3.2 Backpressure & Non-Blocking Semantics
- **Full Buffer Backpressure:** When `bytes_available == capacity`:
  - Blocking write: Returns `0` (indicating backpressure; thread waits or yields).
  - Non-blocking write: Returns `-35` (`-EAGAIN`).
- **Partial Transfers:** Writes larger than available capacity transfer `capacity - count` bytes and return the transferred count.

### 3.3 End-of-File (EOF) & Broken Pipe (EPIPE)
- **EOF Semantics:** When the write end of a pipe is closed (`close_write()`), any unread bytes in the ring buffer can still be drained by the reader. Once empty, subsequent reads deterministically return `0` (`EOF`).
- **Broken Pipe (`EPIPE`):** When the read end of a pipe is closed (`close_read()`), any write attempt immediately fails and returns `-32` (`-EPIPE`). In `UserSyscallDispatcher`, this maps directly to `kErrnoEPIPE` (32) in `X0`.

---

## 4. Syscall Integration Verification

| Syscall | Operation Tested | Verified Outcome |
| :--- | :--- | :--- |
| `SYS_pipe` (42) | Allocate reader/writer pair | Returns reader in `fds[0]`, writer in `fds[1]` |
| `SYS_write` (4) | Transfer payload into pipe | Writes `N` bytes into ring buffer |
| `SYS_read` (3) | Read payload from pipe | Receives exact byte sequence in user memory |
| `SYS_close` (6) | Close read end of pipe | Triggers `close_read()` on underlying pipe |
| `SYS_write` (4) | Write to reader-closed pipe | Dispatcher returns `kErrnoEPIPE` (32) |
