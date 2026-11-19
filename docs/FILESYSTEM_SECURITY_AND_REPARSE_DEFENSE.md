# WinIOS Filesystem Security & Reparse Point Defense
## Campaign 232 — Security Architecture Reference

This reference outlines the filesystem isolation, path validation, and Windows-specific attack mitigation models implemented in `src/packaging/canonical_path_validator.h/.cpp`.

---

## 1. Threat Model for Windows-Hosted Filesystems

When running on Windows 11 host operating systems, POSIX guest paths map to physical NTFS or ReFS paths. This introduces several Windows-specific attack surfaces that could permit a guest application to escape its sandbox:
1. **NTFS Directory Junctions & Reparse Points**: Symbolic links or mount points redirecting writes outside `/apps` or `/data`.
2. **Win32 Device Namespaces (`\\?\`, `\\.\`)**: Bypassing path normalization and length limits to target raw volumes or system devices.
3. **8.3 Short File Names**: Using abbreviated aliases (`PROGRA~1`, `APPDIR~1`) to bypass blacklist or sandbox prefix filters.
4. **DOS Device Names**: Targeting special devices like `CON`, `NUL`, `AUX`, `COM1`, or `LPT1`.

---

## 2. Hardened Validation Rules in `CanonicalPathValidator`

### Rule 1: Reparse Point Detection
`CanonicalPathValidator::is_reparse_point(path)` queries filesystem attributes using `GetFileAttributesW`:
- If `FILE_ATTRIBUTE_REPARSE_POINT` is set, the path is immediately rejected.
- Directory traversal checks ensure no intermediate directory component in the target hierarchy is a reparse point or junction.

### Rule 2: Physical Path Resolution
To ensure paths resolve strictly within the designated sandbox root:
- The validator calls `GetFinalPathNameByHandleW` with `FILE_NAME_NORMALIZED`.
- The resolved physical volume path is compared against the normalized sandbox base directory.
- Any discrepancy triggers `InvalidCanonicalPath`.

### Rule 3: Win32 & NT Namespace Rejection
Any path beginning with:
- `\\?\` (Win32 Extended Namespace)
- `\\.\` (Win32 Device Namespace)
- `\??\` (NT Object Manager Namespace)
- `//?/` or other alternate slash forms
is immediately rejected before parsing.

### Rule 4: Short-Name Alias Rejection
Any path component matching the pattern `*[~][0-9]*` is rejected to prevent short-name alias evasion.

---

## 3. Best Practices for Developers

- Always use canonical POSIX relative paths (`assets/config.json`) within package manifests.
- Avoid trailing dots, trailing spaces, or reserved Windows characters (`<`, `>`, `:`, `"`, `|`, `?`, `*`).
- Never rely on symbolic links inside application packages; packages containing symlinks will fail installation verification.
