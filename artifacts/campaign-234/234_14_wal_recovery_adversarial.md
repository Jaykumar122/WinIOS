# Stage 234.14: WAL Recovery Idempotence and Corruption Resilience

## Executive Summary
Stage 234.14 subjected the Write-Ahead Log recovery subsystem (`PackageInstaller::reconcile_and_recover`) to adversarial corruption conditions, verifying transaction recovery idempotence, partial/truncated write tolerance, journal integrity checksum enforcement, backup restoration, and bidirectional synchronization between the application registry and filesystem state.

---

## 1. Adversarial Scenarios Tested

### 1.1 Recovery Idempotence
- **Scenario:** An abandoned in-flight transaction directory `/staging/tx_abandoned` with a valid intermediate journal state (`Extracting`) was recovered.
- **Verification:**
  - Run 1 successfully cleaned up the abandoned staging transaction (`abandoned_transactions_cleaned == 1`).
  - Run 2 and Run 3 were executed immediately on the recovered state.
  - Both subsequent runs returned `total_recovered() == 0` and made zero state modifications, confirming that recovery is strictly idempotent ($f(f(x)) = f(x)$).

### 1.2 Truncated & Corrupted Journals
- **Truncated JSON:** Injected a journal file cut off mid-key (`{"tx_id": "tx_trunc", "bundle_id": "com.trunc`). Recovery parsed the malformed JSON safely without throwing or terminating abnormally, identified it as an invalid staging transaction, unlinked the staging directory, and recorded an abandoned cleanup.
- **Corrupted Checksum:** Injected a journal where the `checksum` field was modified to `0xDEADBEEF...`. Recovery calculated the expected SHA-256 integrity hash, detected `!j.checksum_valid`, and safely aborted the transaction.

### 1.3 Backup Directory Restoration
- **Scenario:** Simulated a fatal crash during an application update transaction after existing files had been moved to `/backup/backup_app` and while staging new files.
- **Verification:** Recovery inspected the journal, recognized `is_update == true`, verified the backup directory existed, and atomically restored `/backup/backup_app` back to `/Applications/com.update.app.app`, guaranteeing zero data loss.

### 1.4 Bidirectional Registry / Filesystem Desync Reconciliation
- **Ghost Application:** Registered an application in `ApplicationRegistry` whose target folder in `/Applications` did not exist. Recovery detected the missing directory, set its installation state to `InstallationState::Corrupted`, deactivated it, and flagged `corrupted_apps_flagged == 1`.
- **Unindexed Filesystem Bundle:** Planted an authentic application bundle in `/Applications/com.discovered.app.app` without a registry entry. Recovery discovered the bundle, registered it with `InstallationState::Installed`, activated it, and recorded `unindexed_apps_discovered == 1`.

---

## 2. Test Verification
The dedicated suite `tests/unit/stage234_14_wal_recovery_adversarial_tests.cpp` was compiled with MSVC C++20 (`/W4 /WX /permissive-`) and executed:
- `RelWithDebInfo`: Passed 100%
- `Debug`: Passed 100%
