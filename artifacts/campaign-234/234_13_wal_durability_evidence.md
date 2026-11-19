# Stage 234.13: WAL Write Ordering and Durability Evidence

## Executive Summary
Stage 234.13 presents an empirical and architectural audit of Write-Ahead Log (WAL) write ordering, buffer flushing, and durability guarantees in WinIOS. It establishes the exact operational distinction between **Process Termination Durability** (survival of unhandled guest or host process aborts) and **Power-Loss Durability** (survival of sudden host hardware power loss or kernel panics).

---

## 1. Operating System Primitives & Write Ordering

### 1.1 Atomic Staging & Replacement Pattern
On Windows host storage (`src/runtime/session_transaction.cpp` and `src/packaging/canonical_path_validator.cpp`), atomic durability follows a strict 4-step sequence:
```
  [Step 1: Open Temporary File]
    CreateFileW(temp_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)
          |
          v
  [Step 2: Synchronous Write]
    WriteFile(h, text.data(), text.size(), &written, nullptr)
          |
          v
  [Step 3: Cache Invalidation & Disk Flush]
    FlushFileBuffers(h)
          |
          v
  [Step 4: Atomic Rename with Write-Through]
    CloseHandle(h)
    MoveFileExW(temp_path, final_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
```

### 1.2 Purpose of `FlushFileBuffers`
1. `WriteFile` transfers user-mode buffer bytes to the Windows kernel system cache (`CcCopyWrite`).
2. Without an explicit flush, dirty pages remain in the operating system cache until the Windows lazy writer thread flushes them (typically within 1–5 seconds).
3. `FlushFileBuffers(h)` forces the Windows Cache Manager to immediately issue `IRP_MJ_FLUSH_BUFFERS` to the underlying filesystem driver (NTFS) and storage miniport (storport/NVMe). The calling thread blocks until the driver confirms dispatch.

### 1.3 Purpose of `MOVEFILE_WRITE_THROUGH`
1. When replacing an existing destination file (`MOVEFILE_REPLACE_EXISTING`), NTFS updates the parent directory B-tree index in the NTFS Master File Table (MFT) and `$LogFile`.
2. `MOVEFILE_WRITE_THROUGH` guarantees that NTFS flushes directory metadata changes to the disk controller prior to returning success, ensuring the newly linked directory entry is persistent.

---

## 2. Process Termination vs Power-Loss Durability

| Durability Dimension | Process Termination (Crash / SIGKILL / TerminateProcess) | Power-Loss Event (Sudden Power Cut / Kernel BSOD) |
| :--- | :--- | :--- |
| **Data in User Buffers** | Lost immediately upon process termination. | Lost immediately. |
| **Data in OS Page Cache** | **Preserved:** Windows kernel Cache Manager retains dirty pages and commits them asynchronously to disk. | **Lost:** Volatile RAM loses all unwritten dirty pages. |
| **Data with `FlushFileBuffers`** | **Preserved:** Already committed to storage driver prior to process termination. | **Preserved:** Committed to drive controller non-volatile write queue. |
| **Interrupted Pre-Commit** | Transaction directory left in `/staging/<tx_id>`; cleaned up automatically by `reconcile_and_recover()`. | Partial files or temp files detected by journal checksum mismatch; aborted on recovery. |
| **Interrupted Rename** | NTFS guarantees rename atomicity: destination retains either old state or new state; never corrupted middle state. | NTFS metadata journaling (`$LogFile`) recovers directory consistency on boot. |

---

## 3. Emulation / Virtual Filesystem (VFS) Boundary
- In the in-memory `VirtualFileSystem` layer (`src/packaging/package_installer.cpp`), WAL durability models the identical 11-state transition ordering.
- Rollback mechanisms (`rollback_staging()`) ensure that in-flight staging directories are pruned on error.
- Physical host persistence is governed by the Windows host filesystem integration (`CreateFileW`, `FlushFileBuffers`, `MoveFileExW`).

---

## 4. Test Verification
The WAL durability fault-injection suite `tests/unit/stage233_13_wal_durability_tests.cpp` verifies:
- Crash injection across all 8 pre-commit forward installer states.
- Clean abandoned-transaction detection and rollback on startup.
- Complete preservation of existing apps during interrupted update transactions.
