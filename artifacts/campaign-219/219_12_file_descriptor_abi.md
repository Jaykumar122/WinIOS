# WinIOS Campaign 219 — Stage 219.12: VFS File Descriptor Subsystem & ABI

## 1. Per-Process File Descriptor Table

Every `BsdProcess` owns a dedicated `FileDescriptorTable` (`fd_table_`), encapsulating up to 64 active file descriptors (`kMaxFiles = 64`):

```
Index 0: /dev/console (Standard Input, Readable)
Index 1: /dev/console (Standard Output, Writable)
Index 2: /dev/console (Standard Error, Writable)
Index 3..63: Regular files, Pipes, Memory mapped streams
```

## 2. Descriptor Allocation & Duplication (`SYS_dup`, 41)

1. **Allocation**: `allocate_fd(type, readable, writable, path)` scans from index 0 upwards to locate the lowest-numbered unallocated slot, conforming to POSIX and Darwin specifications.
2. **Duplication**: `duplicate_fd(old_fd)` allocates the lowest available free descriptor slot and copies the underlying descriptor reference (sharing VFS stream or `Pipe` shared pointer).
3. **Closure**: `close_fd(fd)` resets the slot and triggers endpoint closure (`close_read()`, `close_write()`, or `vfs->close()`).

## 3. Test Verification

Verified by `stage219_13_vfs_fd_tests.exe`:
- Correct initialization of standard descriptors 0, 1, 2.
- Allocation of descriptor 3, close of descriptor 3, and reuse of slot 3 on subsequent allocation.
- Duplication of descriptor 3 into slot 4 via `SYS_dup` (SYS 41) verifying shared stream properties.
