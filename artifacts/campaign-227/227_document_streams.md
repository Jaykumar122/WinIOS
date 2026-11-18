# WinIOS Campaign 227: Document Read/Write Streams & Atomic Save Architecture

## 1. Overview

WinIOS Document Streams provide buffered, bounded, sequential or random-access I/O channels over registered documents. Document Streams abstract physical storage while guaranteeing transaction integrity, quota safety, and atomic commit semantics.

---

## 2. Stream Lifecycle & 64-Bit Stream Handles

Streams are identified by 64-bit generation handles (`StreamHandle`):
- `id` (bits 31..0): Monotonic stream identifier.
- `gen` (bits 63..32): Generation counter incremented on close.

### 2.1 Syscall Interfaces

1. `SYS_WINIOS_DOC_STREAM_OPEN (574)`: Opens a stream in `Read`, `Write`, or `ReadWrite` mode.
2. `SYS_WINIOS_DOC_STREAM_READ (575)`: Copies bytes from the stream buffer into caller address space.
3. `SYS_WINIOS_DOC_STREAM_WRITE (576)`: Copies bytes from caller address space into the stream buffer.
4. `SYS_WINIOS_DOC_STREAM_SEEK (577)`: Sets the byte cursor position within bounded limits.
5. `SYS_WINIOS_DOC_STREAM_CLOSE (578)`: Flushes modified buffers to backing storage atomically and invalidates the stream handle.

---

## 3. Atomic Commit & Crash Resilience

To prevent partial writes or document corruption if an application crashes mid-operation:

1. **In-Memory Staging:** During active `write_stream` operations, modifications accumulate in the stream's internal memory buffer.
2. **Crash-Safe Staging File:** Upon `flush_stream` or `close_stream`, the buffer is written to an ephemeral staging path in the VFS:
   ```
   <canonical_path>.tmp.<pid>.<gen>
   ```
3. **Atomic Promotion:** Once the staging write completes successfully, the staging file is atomically moved/promoted to `<canonical_path>` via VFS rename semantics.
4. **Recovery Sentinel:** If a process crashes while a stream is dirty, the Document Stream Manager detects leftover `.tmp` staging artifacts on startup and either rolls back or commits the completed staging block.

---

## 4. Quota & Bounded Stream Enforcement

To safeguard the virtual environment from denial-of-service and runaway memory exhaustion:
- **Maximum Document Size:** Strictly capped at `kMaxDocumentSizeBytes = 10 * 1024 * 1024` (10 MB).
- **Write Overflow Guard:** If `cursor + len > kMaxDocumentSizeBytes`, the write operation is rejected with `false` (`DocumentErrorCode::QuotaExceeded`).
- **Integer Overflow Protection:** Arithmetic expressions `cursor + len` verify `stream.cursor <= UINT64_MAX - len` prior to addition.
- **Seek Bounds:** Seeking beyond `kMaxDocumentSizeBytes` is rejected with `false`.
