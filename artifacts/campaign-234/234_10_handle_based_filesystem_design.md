# Stage 234.10: Handle-Based Windows Filesystem Confinement Design & Implementation

## Executive Summary
Stage 234.10 addresses the fundamental vulnerability of lexical path validation against Time-of-Check to Time-of-Use (TOCTOU) filesystem race conditions, symlink substitution, and directory junction jailbreaks. While lexical normalization verifies string patterns, it cannot prevent an adversary or concurrently running process from swapping a directory with an NTFS junction or reparse point between the time a path is validated and the time a physical file is opened.

To close this architectural gap, WinIOS has implemented **Handle-Based Root Confinement** in `CanonicalPathValidator` (`src/packaging/canonical_path_validator.h` and `src/packaging/canonical_path_validator.cpp`).

---

## 1. Architectural Vulnerability of Lexical-Only Confinement

In purely lexical path sandboxing:
```
  [1. Path String] ---> [2. String Match Check] ---> [3. OS File Open (Path)]
```
If an attacker creates an NTFS junction or directory symlink at `root/subdir -> C:\Windows\System32`:
- A lexical check of `root/subdir/file.txt` sees `root/...` as a prefix and passes.
- When `CreateFile` opens `root/subdir/file.txt`, the Windows Object Manager traverses the junction, writing to `C:\Windows\System32\file.txt`.
- Even if `is_reparse_point` is checked prior to open, a concurrent thread or process can replace the directory between step 2 and step 3 (TOCTOU race).

---

## 2. Handle-Based Confinement Architecture

Handle-based confinement moves verification from the path string to the **kernel file object handle**:
```
  [1. Target Path] ---> [2. CreateFileW (FILE_FLAG_BACKUP_SEMANTICS)]
                                |
                                v
               [3. GetFinalPathNameByHandleW]
                                |
                                v
      [4. Kernel Identity Verified Strictly Inside Root Handle]
              /                                      \
         [Match: Keep Handle]               [Escape: CloseHandle & Abort]
```

### Key Security Primitives Implemented:

1. **UTF-8 to UTF-16 Conversion (`utf8_to_wide` / `wide_to_utf8`):**
   - Eliminates legacy ANSI code-page truncation (`CreateFileA`/`GetFinalPathNameByHandleA`) by using Windows Unicode APIs (`CreateFileW`/`GetFinalPathNameByHandleW`).

2. **Handle Identity Query via `GetFinalPathNameByHandleW`:**
   - Opens the physical handle with `FILE_FLAG_BACKUP_SEMANTICS` and queries `FILE_NAME_NORMALIZED | VOLUME_NAME_DOS`.
   - Resolves all intermediate NTFS junctions, volume mount points, symlinks, and 8.3 short-name aliases down to the true canonical device volume path.

3. **Handle-to-Handle Confinement (`is_handle_strictly_inside_root_handle`):**
   - Queries the canonical root path from the open root handle and canonical target path from the open target handle.
   - Compares the volume and normalized directory hierarchy. Any deviation, cross-volume link, or escape outside the root prefix causes immediate handle closure and failure.

4. **Confined File Operations (`open_confined_file_read` / `open_confined_file_write`):**
   - **Atomic Parent Check:** When creating files, parent directory confinement is validated prior to creation.
   - **Post-Creation Invariant:** Immediately after `CreateFileW`, the open `HANDLE` is queried via `GetFinalPathNameByHandleW`. If the resulting file object is not strictly inside the canonical root, the handle is closed immediately and the newly created artifact is unlinked, defeating creation-time symlink swaps.

---

## 3. Implementation Summary
- `CanonicalPathValidator::is_handle_strictly_inside_root(void* file_handle, const std::string& root_dir)`
- `CanonicalPathValidator::is_handle_strictly_inside_root_handle(void* file_handle, void* root_dir_handle)`
- `CanonicalPathValidator::open_confined_file_read(const std::string& root_dir, const std::string& rel_path)`
- `CanonicalPathValidator::open_confined_file_write(const std::string& root_dir, const std::string& rel_path, bool create_new)`
- `CanonicalPathValidator::close_confined_file(ConfinedFileHandle& confined)`

All components build with zero warnings under MSVC `/W4 /WX /permissive-`.
