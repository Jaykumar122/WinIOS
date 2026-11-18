# WinIOS Campaign 219 — Stage 219.13: Bounded Inter-Process Pipe Subsystem

## 1. Unidirectional Pipe Architecture

The `Pipe` primitive (`src/kernel/pipe.h`) provides bounded FIFO byte stream communication between threads and processes:

- **Buffer Structure**: Circular ring buffer with fixed capacity of 64 KiB (`kDefaultCapacity = 65536`).
- **Synchronization**: Thread-safe access via internal `std::mutex`.
- **Flow Control**:
  - `write()` transfers up to available capacity, updating write head.
  - `read()` transfers available queued bytes, advancing read tail.
- **Broken Pipe Detection**:
  - If the read end is closed (`close_read()`), any subsequent `write()` returns `-32` (`-EPIPE`).
  - If the write end is closed (`close_write()`) and all buffered bytes have been drained, `read()` returns 0 (`EOF`).

## 2. Verification

Verified in `stage219_13_vfs_fd_tests.exe`:
- FIFO byte ordering and capacity tracking (`bytes_available()`, `space_available()`).
- Reader EOF detection upon writer closure.
- Broken pipe `-EPIPE` return upon reader closure.
