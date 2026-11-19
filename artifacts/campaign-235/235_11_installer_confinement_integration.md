# Stage 235.11 — Package Installer Confinement Integration

## 1. Executive Summary
Stage 235.11 verified the end-to-end integration of `CanonicalPathValidator` within `PackageInstaller`. The package installer verifies every file entry in `pkg.manifest.files` against bundle security rules prior to file creation or staging directory allocation. Packages attempting path traversal, DOS reserved device name spoofing, Alternate Data Streams (ADS), drive-letter escapes, or reparse point injection are rejected at the `Validating` stage with `InstallStatus::InvalidManifest`, triggering immediate rollback and cleanup without contaminating the host or VFS filesystem.

## 2. Integration Architecture

### 2.1 Manifest Confinement Verification
In `PackageInstaller::install` and `PackageInstaller::update`:
```cpp
for (const auto& f : pkg.manifest.files) {
    std::string norm_path;
    if (CanonicalPathValidator::validate_and_normalize_bundle_path(f.path, norm_path) != PathValidationStatus::Valid) {
        write_journal(tx_id, bundle_id, InstallerState::Aborted, ...);
        rollback_staging(staging_tx);
        return InstallStatus::InvalidManifest;
    }
}
```
All manifest entries must satisfy:
1. Relative path without leading slashes or drive letters.
2. No `..` or escaping directory traversals.
3. No Windows reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`).
4. No Alternate Data Streams (`:`).
5. No 8.3 short names (`~`).
6. Maximum path length $\le 256$ and maximum directory depth $\le 16$.

### 2.2 Staging and Atomic Commit
- Legitimate packages extract strictly under `/staging/<tx_id>/bundle/<norm_path>`.
- The final commit renames `/staging/<tx_id>/bundle` to `/Applications/<bundle_id>.app` atomically.
- If validation fails, `rollback_staging` purges the transaction staging directory completely.

## 3. Test Evidence & Results
- Target: `stage235_11_installer_confinement_tests`
- File: `tests/unit/stage235_11_installer_confinement_tests.cpp`
- **Evaluated Test Cases:**
  1. `test_legitimate_package_installation`: PASSED (installed to `/Applications/<bundle_id>.app/`).
  2. `test_path_traversal_manifest_rejection`: PASSED (`../escape.txt` rejected with `InvalidManifest`).
  3. `test_windows_device_manifest_rejection`: PASSED (`bin/CON` rejected with `InvalidManifest`).
  4. `test_alternate_data_stream_manifest_rejection`: PASSED (`bin/main:hidden` rejected with `InvalidManifest`).
  5. `test_drive_letter_manifest_rejection`: PASSED (`C:/windows/cmd.exe` rejected with `InvalidManifest`).
- **Performance:**
  - RelWithDebInfo: 0.06s (PASSED 100%)
  - Debug: 0.06s (PASSED 100%)
  - Compiler Compliance: 0 warnings, 0 errors under `/W4 /WX /permissive-`.
