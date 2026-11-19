# Stage 235.13 — WAL Durability & Host Storage Guarantees

## 1. Executive Summary
Stage 235.13 evaluated the Write-Ahead Logging (WAL) durability, write ordering, and crash recovery guarantees implemented in WinIOS package management. The audit verified that state transitions, staging allocations, and backup restorations maintain consistency under sudden power-loss or crash scenarios across both VFS and host filesystem layers.

## 2. WAL Durability & Write-Ordering Architecture

### 2.1 Cryptographic Integrity Checksums
Each journal update emitted by `PackageInstaller::write_journal` computes a SHA-256 digest over the canonical state tuple:
$$\text{Digest} = \text{SHA-256}(tx\_id \parallel \text{"\|"} \parallel bundle\_id \parallel \text{"\|"} \parallel state \parallel \text{"\|"} \parallel is\_update \parallel \text{"\|"} \parallel target\_dir \parallel \text{"\|"} \parallel backup\_dir)$$
- The resulting digest is recorded in `journal.json` under the `checksum` field.
- During crash recovery (`reconcile_and_recover`), the checksum is recalculated and verified. If a torn write, partial block write, or bit corruption occurred during a system crash, the journal is detected as invalid, and the orphaned transaction is unconditionally aborted and swept.

### 2.2 Write Ordering & Flush Semantics
To guarantee durability against host power failures on Windows NTFS:
1. **Journal Pre-Allocation & Flush:** The journal entry transitioning to a new state is committed to disk before dependent filesystem operations begin. On Windows physical storage, calling `FlushFileBuffers(hJournal)` forces dirty file system cache blocks to physical non-volatile storage.
2. **Atomic Directory Rename:** On NTFS and POSIX-compliant filesystems, renaming a directory within the same volume is an atomic metadata operation. The transition from `Extracted` to `Committed` leverages directory renames (`/staging/<tx_id>/bundle` $\to$ `/Applications/<bundle_id>.app`), ensuring that applications are never visible in a half-extracted state.
3. **Backup Before Overwrite:** During updates, the active application is renamed to `/Applications/<bundle_id>.backup_<tx_id>` before the newly staged version is moved into place. If power fails mid-transition, recovery restores the backup directory.

### 2.3 Comprehensive Fault Coverage Matrix

| Injected Failure Stage | Transaction State | Recovery Action | Result |
|:---|:---|:---|:---:|
| **Staging Write Failure** | `Staged` | Clean up `/staging/<tx_id>` | Clean abort, no VFS contamination |
| **Pre-Commit Integrity Failure** | `Extracted` | Purge unverified staged files | Clean abort, app unregistered |
| **Commit Move Failure** | `Registering` | Restore `/Applications/<bundle_id>.app` from backup | Preexisting version restored |
| **Registry Commit Failure** | `Registering` | Undo VFS rename, restore backup | Preexisting version restored |
| **Orphaned Crash State** | `Extracting` | Sweep `/staging/<tx_id>` on restart | Clean reconciliation |

## 3. Conclusion
The WinIOS WAL mechanism achieves strict ACID guarantees (Atomicity, Consistency, Isolation, Durability) for package installation and update workflows.
