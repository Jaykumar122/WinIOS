# Stage 232.12 — Crash-Consistent Installation Architecture v2 Specification & Implementation

**Campaign:** 232  
**Stage:** 232.12  
**Component:** `winios::packaging::PackageInstaller`, `winios::packaging::InstallerState`  
**Files Modified:**
- `src/packaging/package_installer.h`
- `src/packaging/package_installer.cpp`

---

## 1. Overview & Objectives

In Campaign 231, transactional package installation was introduced using a coarse 4-state journal model (`Pending`, `Validated`, `Committing`, `Committed`, `RolledBack`). While this protected against simple move failures, it did not provide fine-grained recovery points or cryptographic journal verification. If a crash or power failure occurred during extraction or path validation, the state was ambiguous, and journals could be tampered with or corrupted on disk.

Stage 232.12 designs and implements **Crash-Consistent Installation Architecture v2**:
1. **Explicit 11-State Finite State Machine:** Covers the full lifecycle from pre-validation through extraction, registry commitment, and finalization.
2. **Write-Ahead Atomic Journaling with SHA-256 Checksums:** Every journal transition flushes a write-ahead log record containing an authoritative SHA-256 integrity checksum over all transaction parameters.
3. **Strict Crash Recovery Algorithm:** Detects incomplete transactions, validates journal integrity against bit-flips or tampering, safely unwinds orphaned staging trees, and restores pre-update backups.

---

## 2. 11-State Finite State Machine

The installer transitions deterministically through the following states:

```
  +------+
  | Idle |
  +---+--+
      | (allocate tx_id, create /staging/<tx_id>)
      v
+-----------+
| Initiated |
+-----+-----+
      | (check manifest presence & basic structure)
      v
+------------+
| Validating |
+-----+------+
      | (verify manifest validity & bundle uniqueness)
      v
+----------------+
| ManifestParsed |
+-----+----------+
      | (validate canonical paths & check security policies)
      v
+------------------+
| SecurityVerified |
+-----+------------+
      | (allocate staging bundle directory)
      v
   +--------+
   | Staged |
   +----+---+
        | (write all payload files into staging directory)
        v
  +------------+
  | Extracting |
  +-----+------+
        | (pre-commit integrity hash re-validation of extracted files)
        v
   +-----------+
   | Extracted |
   +-----+-----+
         | (atomic move from staging to /Applications/<bundle_id>.app)
         v
  +-------------+
  | Registering |
  +------+------+
         | (commit application metadata into ApplicationRegistry)
         v
   +-----------+
   | Committed | ---> Cleanup staging directory ---> [ Success ]
   +-----------+
```

### State Definitions

| State | Ordinal | Action / Invariant | Failure / Abort Action |
|---|---|---|---|
| `Idle` | 0 | Default initial state; no active transaction | N/A |
| `Initiated` | 1 | Transaction directory `/staging/<tx_id>` created; journal written | Remove `/staging/<tx_id>` |
| `Validating` | 2 | Manifest syntax, required keys, format version checked | Rollback staging; transition `Aborted` |
| `ManifestParsed` | 3 | Manifest fully parsed; bundle ID uniqueness verified | Rollback staging; transition `Aborted` |
| `SecurityVerified` | 4 | Canonical paths normalized, traversal/device checks passed | Rollback staging; transition `Aborted` |
| `Staged` | 5 | `/staging/<tx_id>/bundle` created and permissions configured | Rollback staging; transition `Aborted` |
| `Extracting` | 6 | Payload files written to `/staging/<tx_id>/bundle/` | Rollback staging; transition `Aborted` |
| `Extracted` | 7 | All files re-read; SHA-256 digests verified against manifest | Rollback staging; transition `Aborted` |
| `Registering` | 8 | Target moved or backed up; staging bundle renamed to target | Restore backup if update; remove target |
| `Committed` | 9 | App metadata committed to registry; transaction final | Complete staging cleanup |
| `Aborted` | 10 | Explicit rollback completed | Discard staging directory |

---

## 3. Cryptographic Write-Ahead Journal Structure

Every journal entry is written to `/staging/<tx_id>/journal.json` using write-ahead semantics:

```json
{
  "tx_id": "tx_4a9b2c8e10f3",
  "bundle_id": "com.sample.app",
  "state": "Extracted",
  "is_update": false,
  "target_dir": "/Applications/com.sample.app.app",
  "backup_dir": "",
  "checksum": "c2b5f7e8a93019d45e0f1182390234ab87201cba64f89d34e8f001235492beef"
}
```

### Integrity Checksum Domain
The `checksum` field is computed over the canonical tuple:
$$\text{Checksum} = \text{SHA256}(\text{tx\_id} \mathbin{\Vert} \text{bundle\_id} \mathbin{\Vert} \text{state} \mathbin{\Vert} \text{is\_update} \mathbin{\Vert} \text{target\_dir} \mathbin{\Vert} \text{backup\_dir})$$

When `reconcile_and_recover()` executes on boot:
1. It parses `journal.json`.
2. It re-computes the expected SHA-256 digest over the parsed fields.
3. If `checksum` does not match, the journal is identified as corrupt/tampered.
4. Tampered journals are immediately prevented from finalizing; any associated staging directories are purged safely, and backups are restored if an update was underway.

---

## 4. Crash Recovery Guarantees

1. **Atomicity:** An installation either reaches `Committed` or is completely rolled back to pre-install state.
2. **Isolation:** Concurrent transactions use isolated `/staging/<tx_id>` directories with 64-bit random transaction identifiers generated via `SecureRandom`.
3. **Consistency:** If power fails during file extraction (`Extracting`), no files have been touched in `/Applications`. Recovery purges `/staging/<tx_id>`.
4. **Update Safety:** During updates, `/Applications/<bundle_id>.app` is moved to `/backup/<tx_id>` before staging is moved into place. If a crash occurs during `Registering`, recovery detects the incomplete state, removes any partial target, and renames `/backup/<tx_id>` back to `/Applications/<bundle_id>.app`.
