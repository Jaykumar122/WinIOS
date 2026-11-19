# WinIOS Campaign 233 — Stage 233.10 Deliverable
## Windows Host Filesystem Security & Sandbox Escape Vulnerability Review

### 1. Executive Summary
This security review analyzes Windows-specific filesystem attacks and Time-of-Check to Time-of-Use (TOCTOU) hazards targeting the WinIOS application installer and guest sandbox runtime.

Target Components: `src/packaging/canonical_path_validator.cpp`, `src/packaging/package_installer.cpp`.

---

### 2. Threat Catalog & Architectural Defenses

#### 2.1 NTFS Reparse Points & Directory Junctions
- **Attack Vector:** An attacker or malicious package pre-creates an NTFS directory junction (`IO_REPARSE_TAG_MOUNT_POINT`) or symbolic link (`IO_REPARSE_TAG_SYMLINK`) inside the target extraction directory pointing to a privileged host location (e.g., `%SYSTEMROOT%` or `%APPDATA%`). Subsequent file extractions write through the junction, overwriting host binaries.
- **Defenses in `CanonicalPathValidator`:**
  - `is_reparse_point` queries `FILE_ATTRIBUTE_REPARSE_POINT` via `GetFileAttributesA`.
  - `resolve_canonical_physical_path` opens the handle with `FILE_FLAG_BACKUP_SEMANTICS` and calls `GetFinalPathNameByHandleA(VOLUME_NAME_DOS | FILE_NAME_NORMALIZED)`, resolving the true physical device path.
  - `is_strictly_inside_root` walks the parent directory hierarchy and asserts that the physical target path begins strictly with the physical root path.

#### 2.2 Alternate Data Streams (ADS)
- **Attack Vector:** Appending stream specifiers (e.g., `payload.dll:stream` or `container:$INDEX_ALLOCATION`) to bypass antivirus scanners, conceal code, or disrupt NTFS directory indices.
- **Defenses:**
  - `validate_and_normalize_bundle_path` and `validate_and_normalize_vfs_path` scan for `:` characters.
  - Colons outside valid drive letters (e.g. `C:`) immediately fail with `PathValidationStatus::AlternateDataStreamForbidden`.

#### 2.3 Windows Reserved Device Names & Device Namespaces
- **Attack Vector:**
  - Opening DOS device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`) causes the Win32 subsystem to block on console input or write to serial ports.
  - Windows NT device namespace prefixes (`\\?\`, `\\.\`, `\??\`) bypass standard path canonicalization and `MAX_PATH` bounds.
- **Defenses:**
  - `contains_device_namespace` checks for `\\?\`, `//?/`, `\\.\`, `//./`, `\??\`, and `/??/`, returning `DeviceNamespaceForbidden`.
  - `is_windows_reserved_device_name` strips file extensions and case-insensitively checks segment base names against all reserved devices, returning `WindowsReservedDevice`.

#### 2.4 8.3 Short Name (SFN) Aliasing
- **Attack Vector:** On volumes with SFN enabled, NTFS auto-generates 8.3 aliases (e.g., `APP_SU~1` for `app_supervisor`). An attacker references the short name to circumvent path filters or overwrite unintended files.
- **Defenses:**
  - `contains_short_name_alias` scans for `~` followed by numeric digits (`~[0-9]`), returning `PathValidationStatus::ShortNameForbidden`.

#### 2.5 Trailing Dots & Whitespace Stripping
- **Attack Vector:** Windows Win32 APIs silently strip trailing spaces and dots (e.g. `file.txt.` becomes `file.txt`). An archive containing both `file.txt` and `file.txt.` can cause unexpected file collisions or filter evasion.
- **Defenses:**
  - Segments ending in `.` or space `' '` trigger `PathValidationStatus::TrailingDotOrSpace`.

#### 2.6 TOCTOU Race Resistance
- **Attack Vector:** Between path validation and file extraction, an external process replaces an intermediate directory with a junction.
- **Defenses:**
  - Transactional package installation extracts files into a fresh GUID-named quarantine directory with restrictive ACLs.
  - Directory rename/commit is performed via atomic `MoveFileExA` (`MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`).
  - Pre-commit check re-verifies that no intermediate paths were replaced with reparse points.

---

### 3. Conclusion & Conformance Status
The `CanonicalPathValidator` and `PackageInstaller` implement comprehensive defense-in-depth against all documented Windows-specific filesystem attacks.
