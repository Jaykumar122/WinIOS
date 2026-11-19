# Campaign 231 — Stage 231.7: Crash-Consistent Package Installation

## Objective
Refactor `PackageInstaller` into a fully crash-consistent transactional engine featuring:
1. Transaction isolation with unique CSPRNG-generated transaction directories: `/staging/<tx_id>/`.
2. Durable transaction journal at `/staging/<tx_id>/journal.json` tracking explicit lifecycle states:
   `Pending -> Validated -> Staged -> Committing -> Committed | RolledBack`.
3. Staged write phase extracting all files into `/staging/<tx_id>/bundle/` using strict `CanonicalPathValidator` normalized paths without modifying production applications.
4. Pre-commit integrity check re-reading staged files from VFS, computing SHA-256 hashes, and constant-time comparing against package manifests.
5. Atomic commit phase:
   - For updates: atomic rename of production bundle to `/backup/<tx_id>/`.
   - Atomic rename of `/staging/<tx_id>/bundle` to production `/Applications/<bundle_id>.app/`.
   - Atomic registry update.
   - Automatic removal of backup and staging directories upon commit success.
6. Robust rollback: upon any failure during staging, pre-commit, or commit, automatically clean up staging, restore backup directory if moved, write `RolledBack` journal entry, and leave system in a bit-for-bit consistent state.

## Implementation Details
- **Files Modified:**
  - `src/packaging/package_installer.h`: Added `InjectedFailureStage` enum for deterministic failure injection testing, `TransactionState` enum, and journal writing helper declarations.
  - `src/packaging/package_installer.cpp`: Implemented journal logging, transactional staging, pre-commit hash verification, atomic rename commit, and backup rollback restoration.
  - `CMakeLists.txt`: Registered unit test target `stage231_7_crash_consistent_installer_tests`.
  - `tests/unit/stage231_7_crash_consistent_installer_tests.cpp`: Unit tests validating full install lifecycle, staging failure rollback, pre-commit failure rollback, and update commit failure with backup restoration.

## Verification & Test Results
- Test Target: `stage231_7_crash_consistent_installer_tests` (Test #909)
- Configurations Tested:
  - `RelWithDebInfo`: Passed (0.05 sec)
  - `Debug`: Passed (0.06 sec)
- Test Coverage:
  - `test_successful_installation_lifecycle`: Verified installation, VFS layout, file content, registry metadata, and clean staging removal.
  - `test_staging_write_failure_simulation`: Injected failure during staging; verified no production directory created, registry untouched, staging cleaned.
  - `test_precommit_integrity_failure_simulation`: Injected failure at pre-commit; verified clean abort and zero production leakage.
  - `test_update_failure_with_backup_restoration`: Injected failure during update commit; verified original v1 app and registry entry restored intact.
