# Stage 235.7 — Windows Filesystem Confinement Architecture Review

## 1. Executive Summary
Stage 235.7 performed an architectural audit of the filesystem isolation and path validation boundaries in WinIOS, evaluating defenses against path traversal, symlink escapes, NTFS junction redirects, 8.3 short name aliasing, Alternate Data Streams (ADS), reserved DOS device collisions, and Time-of-Check-Time-of-Use (TOCTOU) rename/reparse-point races.

## 2. Threat Model & Attack Vectors on Windows NTFS

| Attack Vector | Mechanism on Windows / NTFS | WinIOS Defense Layer |
|:---|:---|:---|
| **Lexical Traversal** | `../`, `..\\`, mixed separators | `validate_and_normalize_bundle_path` / `validate_and_normalize_vfs_path` resolves relative components and rejects escaping roots. |
| **NTFS Reparse Points / Junctions** | Directory junctions (`mklink /J`) or symlinks (`mklink /D`) pointing outside sandbox root. | Handle-based verification via `GetFinalPathNameByHandleW` queries the true physical device path of the open kernel file object. |
| **8.3 Short Name Aliasing** | `PROGRA~1`, `RUNTIM~1` bypassing string-based blacklists. | Explicit rejection of any path component matching regex/pattern `~[0-9]` via `contains_short_name_alias`. |
| **Alternate Data Streams (ADS)** | `file.txt:stream`, hiding executables or tampering with metadata. | Disallowance of colon `:` characters in bundle and VFS relative segments (`AlternateDataStreamForbidden`). |
| **DOS Reserved Device Names** | `CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`, `CON.txt`. | Strict parsing of segment base names against Win32 reserved device list (`is_windows_reserved_device_name`). |
| **Windows Device Namespaces** | `\\?\`, `\\.\`, `\??\` prefixing paths to bypass win32 path canonicalization. | Immediate rejection if namespace prefix is present (`contains_device_namespace`). |
| **TOCTOU Reparse / Rename Races** | Replacing directory with junction or renaming directory after validation but prior to file operations. | Handle confinement: validating the open handle's physical path (`is_handle_strictly_inside_root`) and holding exclusive file locking. |

## 3. Handle-Based Confinement Architecture
The fundamental limitation of string-based path verification (`is_strictly_inside_root`) is the temporal gap between string resolution and file opening. The handle-based confinement pattern addresses this:
1. **Atomic Open:** `open_confined_file_write` / `open_confined_file_read` opens the target file.
2. **Handle-Bound Identity:** Calls `GetFinalPathNameByHandleW` with `FILE_NAME_NORMALIZED | VOLUME_NAME_DOS` on the active `HANDLE`. Windows kernel resolves all intermediate symlinks, mounts, and reparse points directly from the file system driver's File Object.
3. **Prefix Confinement Check:** Compares normalized physical root against normalized physical file path.
4. **Immediate Revocation:** If the handle points outside `canonical_root`, the handle is closed and any newly created file is deleted before any user data can be written or read.
5. **Exclusive Locking:** Opening with `dwShareMode = 0` guarantees exclusive access during writing, precluding concurrent rename attacks on the opened leaf file.

## 4. Recommendations for Hardening (Stages 235.8–235.11)
1. Add `FILE_FLAG_OPEN_REPARSE_POINT` to `open_confined_file_write` when checking or creating files, to prevent following a leaf junction or symlink.
2. Query `BY_HANDLE_FILE_INFORMATION` or `FILE_ID_INFO` to verify volume serial number consistency between the root directory handle and the target file handle.
3. Implement `is_handle_strictly_inside_root_handle` using open directory handle comparisons to defend against ancestor directory renames during execution.
