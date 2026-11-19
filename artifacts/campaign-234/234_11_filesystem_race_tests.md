# Stage 234.11: Filesystem TOCTOU and Reparse-Point Attack Verification

## Executive Summary
Stage 234.11 validates handle-based Windows filesystem confinement against actual NTFS reparse points, directory junctions, and path-substitution race attacks using real Windows operating system primitives.

## 1. Threat Scenarios Tested

### 1.1 Normal Confined File IO
- Verified that legitimate subpaths (e.g. `sub/data.bin`) under a designated sandbox root directory are opened safely via `open_confined_file_write`, written to, closed, and reopened via `open_confined_file_read`.
- Both read and write handles verify that the kernel file object identity resides strictly within the root hierarchy.

### 1.2 Lexical Traversal Rejection
- Verified that path sequences containing parent navigation (`../escaped.bin`, `../../boot.ini`) are rejected at the bundle normalization boundary (`validate_and_normalize_bundle_path`) before any kernel handle is created.

### 1.3 NTFS Junction / Reparse-Point Jailbreak Defense
- Created an external directory (`outside/`) containing sensitive data (`secret.txt`).
- Created an active NTFS directory junction inside the sandbox root: `root/jailbreak <<===>> outside`.
- **Read Attack:** Attempted to open `root/jailbreak/secret.txt` using `open_confined_file_read`. Even though the requested path lexically appeared within `root`, the Windows kernel resolved the underlying handle to the real physical path `outside/secret.txt`. `GetFinalPathNameByHandleW` detected that the handle resolved outside `root`, immediately closed the handle, and returned `is_valid == false`.
- **Write Attack:** Attempted to create `root/jailbreak/injected.txt` using `open_confined_file_write`. The validator inspected the parent directory handle `root/jailbreak`, recognized that the parent directory resolved outside `root`, refused to create the file, and ensured the outside directory was completely unmodified.

### 1.4 Handle-to-Handle Pair Invariants
- Verified `is_handle_strictly_inside_root_handle`:
  - An open handle to an authentic inside file tested against an open root handle returns `true`.
  - An open handle to an outside file tested against an open root handle returns `false`.
  - Null handles and `INVALID_HANDLE_VALUE` return `false` safely without exception.

## 2. Test Verification
The dedicated suite `tests/unit/stage234_11_filesystem_race_tests.cpp` was compiled with MSVC C++20 (`/W4 /WX /permissive-`) and executed:
- `RelWithDebInfo`: Passed 100%
- `Debug`: Passed 100%
