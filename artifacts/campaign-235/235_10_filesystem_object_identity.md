# Stage 235.10 — Filesystem Object Identity Verification

## 1. Executive Summary
Stage 235.10 evaluated and documented the kernel-level filesystem object identity mechanisms deployed by WinIOS on Windows NTFS to achieve robust sandbox confinement. By moving beyond path string semantics to file-object properties (volume serial numbers, file indices, and normalized final path names), WinIOS guarantees containment even in the presence of adversarial symlink manipulation and namespace alterations.

## 2. Kernel Object Identity Primitives on Windows NTFS

### 2.1 Volume Serial Number Matching
- **API:** `GetFileInformationByHandle` (`dwVolumeSerialNumber`).
- **Guarantee:** Verifies that both the sandbox root directory handle and the target file handle originate from the same NTFS volume partition.
- **Threat Mitigated:** Cross-volume junction escapes (e.g., an attacker creating an NTFS directory junction that maps a sandbox subfolder to an arbitrary drive letter such as `C:\Windows` or `D:\HostRoot`). If the volume serial numbers differ, the operation is immediately rejected.

### 2.2 Normalized Canonical Device Paths
- **API:** `GetFinalPathNameByHandleW(hFile, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS)`.
- **Guarantee:** Queries the Windows Object Manager and filesystem driver for the resolved physical path associated with the open `FILE_OBJECT`.
- **Threat Mitigated:** Symlink traversal, junction redirects, and relative dot-dot bypasses. Because the path is resolved directly from the kernel handle after opening, all intermediate reparse points are fully resolved to their final physical target.

### 2.3 Reparse Point Flag Detection
- **API:** `GetFileInformationByHandle` (`dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT`).
- **Guarantee:** Ensures that any handle opened by `open_confined_file_write` or `open_confined_file_read` does not represent a symbolic link or junction reparse point.
- **Threat Mitigated:** Writing through existing symlinks to overwrite arbitrary host files.

### 2.4 Exclusive Concurrency Locking
- **API:** `CreateFileW(..., dwShareMode = 0, ...)`.
- **Guarantee:** Takes exclusive file locking while writing confined payloads.
- **Threat Mitigated:** TOCTOU race conditions where an adversary attempts to rename, truncate, or substitute the underlying file while data is being streamed to disk.

## 3. Evaluation & Conclusion
The handle-based identity verification primitives provide deterministic security guarantees under Windows NTFS, eliminating reliance on vulnerable user-mode lexical path comparisons.
