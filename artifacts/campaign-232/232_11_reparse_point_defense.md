# Stage 232.11 Deliverable — Hardened CanonicalPathValidator & Reparse-Point Defense

## 1. Overview
Stage 232.11 implements hardened Windows path validation, device namespace filtering, short name alias protection, and handle-based physical canonical path resolution to defend against NTFS directory junction and symbolic link sandbox escapes (`src/packaging/canonical_path_validator.h`, `src/packaging/canonical_path_validator.cpp`), addressing audit finding **SEC-232-03**.

## 2. Implemented Defenses
1. **Device Namespace Rejection (`DeviceNamespaceForbidden`):**
   - Detects and rejects Win32 device namespaces (`\\?\`, `//?/`, `\\.\`, `//./`, `\??\`, `/??/`) that bypass path length limits and standard filesystem access controls.
   - Evaluated before drive letter or colon checks to avoid false categorization.
2. **Short Name (8.3 SFN) Protection (`ShortNameForbidden`):**
   - Detects tilde-digit patterns (e.g. `PROGRA~1`, `DATA~12`) that could alias or collide with protected system directories.
3. **Reparse Point & Directory Junction Defense:**
   - Added `is_reparse_point(path)` checking `FILE_ATTRIBUTE_REPARSE_POINT`.
   - Added `resolve_canonical_physical_path(path, out_canonical)` using Win32 `CreateFileA` with `FILE_FLAG_BACKUP_SEMANTICS` and `GetFinalPathNameByHandleA` (`FILE_NAME_NORMALIZED | VOLUME_NAME_DOS`).
   - Upgraded `is_strictly_inside_root(root, target)`:
     - Physically resolves `root` and `target` to their true canonical hardware paths.
     - For non-existent target files, traverses parent directory chain to ensure no intermediate ancestor is a junction or symlink escaping the sandbox root.
     - Performs normalized case-folded boundary comparison on Windows.

## 3. Test Verification
- Executed unit tests in `tests/unit/stage232_11_hardened_path_validator_tests.cpp` (registered as CTest #931).
- Tests passed:
  1. `test_windows_device_namespaces`: Rejection of `\\?\`, `//?/`, `\\.\`, `\??\`.
  2. `test_short_name_aliases`: Rejection of `PROGRA~1` and `DATA~12`.
  3. `test_reserved_device_names`: Rejection of `CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`.
  4. `test_alternate_data_streams`: Rejection of `:stream` and `::$DATA`.
  5. `test_trailing_dot_or_space`: Rejection of trailing periods and whitespace.
  6. `test_directory_traversals`: Rejection of `../` attempts escaping root.
  7. `test_physical_sandbox_confinement`: True physical path resolution and sandbox verification.
- Build Status: Zero warnings and zero errors under MSVC `/W4 /WX /permissive-` in both Debug and RelWithDebInfo.
