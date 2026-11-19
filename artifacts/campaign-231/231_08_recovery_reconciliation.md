# Campaign 231 — Stage 231.8: Installation Recovery and Integrity Reconciliation

## Objective
Implement automatic crash recovery and full filesystem-to-registry reconciliation in `PackageInstaller`:
1. Automatic detection and cleanup of abandoned transaction directories under `/staging/`.
2. Intelligent recovery for interrupted transactions in the `Committing` state:
   - If target bundle directory exists and contains files, finalize registration in `ApplicationRegistry` and purge leftover backup and staging directories (`committed_transactions_finalized`).
   - If target bundle directory is missing or incomplete, restore original files from `/backup/<tx_id>/` and remove staging (`backups_restored`).
3. Clean cleanup of aborted transactions in `Pending`, `Validated`, `Staged`, or `RolledBack` states, ensuring backup directory restoration if displaced.
4. Bidirectional integrity reconciliation between `ApplicationRegistry` and `/Applications/`:
   - Identify registered applications whose bundle directory is missing on disk, marking them `InstallationState::Corrupted` and deactivating them.
   - Detect valid on-disk bundles in `/Applications/*.app` that lack a registry record, registering and indexing them into `ApplicationRegistry`.

## Implementation Details
- **Files Modified:**
  - `src/packaging/package_installer.h`: Added `RecoveryReport` struct and declared `reconcile_and_recover()`.
  - `src/packaging/package_installer.cpp`: Implemented JSON journal parser, staging transaction scanner, backup restoration, and bidirectional registry reconciliation.
  - `CMakeLists.txt`: Registered unit test target `stage231_8_recovery_reconciliation_tests`.
  - `tests/unit/stage231_8_recovery_reconciliation_tests.cpp`: Unit test suite covering all crash recovery scenarios.

## Verification & Test Results
- Test Target: `stage231_8_recovery_reconciliation_tests` (Test #910)
- Configurations Tested:
  - `RelWithDebInfo`: Passed (0.04 sec)
  - `Debug`: Passed (0.05 sec)
- Test Coverage:
  - `test_abandoned_staging_recovery`: Verified cleanup of abandoned `Staged` transaction without production pollution.
  - `test_commit_recovery_target_complete`: Verified auto-finalization of complete bundle whose commit crashed before registry write.
  - `test_commit_recovery_target_incomplete_restore_backup`: Verified auto-restoration of `/backup/<tx_id>` when target was destroyed/missing during crash.
  - `test_registry_reconciliation_missing_bundle`: Verified detection of deleted on-disk apps, marking `InstallationState::Corrupted`.
  - `test_unindexed_bundle_discovery`: Verified discovery, indexing, and activation of unindexed bundles.
