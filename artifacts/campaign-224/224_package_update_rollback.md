# WinIOS Campaign 224 — Stage 224.8 Package Update and Automated Rollback

## 1. Update Architecture
`PackageInstaller::update` safely upgrades an existing application while protecting against corruption and incomplete updates:

1. **Pre-flight Check:**
   - Application must already be registered in `ApplicationRegistry`.
   - Manifest semantic version must be $\ge$ current version (unless `allow_downgrade` is explicitly requested).
   - If the application is running, the installer signals termination before updating binaries.
2. **Staged Extraction:**
   - New files are extracted and verified into `/staging/<bundle_id>.update/`.
3. **Known-Good Backup:**
   - The current `/Applications/<bundle_id>.app/` folder is backed up to `/staging/<bundle_id>.backup/`.
4. **Atomic Commit & Verification:**
   - Old directory is removed, and new files are committed to `/Applications/<bundle_id>.app/`.
5. **Automated Rollback on Failure:**
   - If commit fails (e.g. out of disk space, I/O failure), the installer restores `/staging/<bundle_id>.backup/` back to `/Applications/<bundle_id>.app/`.
   - The previous version continues running seamlessly, returning `InstallStatus::RollbackSuccess`.
6. **Finalization:**
   - On commit success, temporary backup and staging folders are removed, and `ApplicationRegistry` is updated with the new version, timestamps, and permissions.
