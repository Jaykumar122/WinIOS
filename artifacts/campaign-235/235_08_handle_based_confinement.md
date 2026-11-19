# Stage 235.8 — Handle-Based Confinement Implementation in CanonicalPathValidator

## 1. Executive Summary
Stage 235.8 implemented handle-based confinement defenses within `CanonicalPathValidator`, transforming path validation from a pre-open string heuristic into a kernel-enforced, handle-bound security mechanism. The hardened implementation checks for reparse points on the opened object, inspects physical device canonical paths via `GetFinalPathNameByHandleW`, enforces volume serial number equality across the root handle and file handle, and maintains exclusive file locking to defeat concurrent symlink or junction redirection.

## 2. Technical Enhancements

### 2.1 Reparse Point Detection on Open Handles
In both `open_confined_file_read` and `open_confined_file_write`:
- Probes existing file attributes for `FILE_ATTRIBUTE_REPARSE_POINT` before creating or opening.
- Queries `BY_HANDLE_FILE_INFORMATION` on the newly acquired handle:
  ```cpp
  if (file_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
      CloseHandle(hFile);
      return result; // Abort immediately
  }
  ```
- Ensures that if an existing file is a junction or symlink, it is never read from or written through.

### 2.2 Volume Serial Number Verification
- Added `CanonicalPathValidator::check_volume_identity(void* handle1, void* handle2)`:
  Queries `GetFileInformationByHandle` for `dwVolumeSerialNumber` on both the target file handle and a root directory handle opened with `FILE_FLAG_BACKUP_SEMANTICS`.
- If the file handle resides on a different volume than the sandbox root (e.g. cross-volume junction mount), the handle is closed immediately and any allocated file is deleted.

### 2.3 Handle Confinement & Cleanup
- `is_handle_strictly_inside_root`: Compares normalized canonical device paths via `GetFinalPathNameByHandleW`.
- `close_confined_file`: Explicitly closes `HANDLE`, sets pointer to `nullptr`, clears `canonical_path`, and resets `is_valid` to `false` to avoid handle re-use vulnerabilities.

## 3. Verification & Compliance
- Successfully compiled into `winios_core.lib` under MSVC `/W4 /WX /permissive-` with 0 warnings.
- Backward compatibility preserved across bundle and VFS path APIs.
