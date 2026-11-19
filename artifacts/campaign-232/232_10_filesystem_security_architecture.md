# Stage 232.10 Deliverable — Windows Filesystem Security Architecture

## 1. Executive Summary & Threat Model
WinIOS executes on Windows 11 host environments while providing a POSIX/Darwin-like guest execution abstraction for ARM64 applications. The filesystem isolation boundary between the host operating system and untrusted guest application packages is vulnerable to Windows-specific path semantics and NTFS features if not rigorously defended.

This document establishes the comprehensive threat model, attack vectors, and architectural defenses against Windows filesystem vulnerabilities.

```
       +-------------------------------------------------------------+
       |               Untrusted Package / Guest Syscall             |
       +-------------------------------------------------------------+
                                      |
                                      v
       +-------------------------------------------------------------+
       |                 CanonicalPathValidator                      |
       |  - Normalization (forward slashes, collapse '.' and '..')   |
       |  - Device Names (CON, PRN, AUX, NUL, COM1-9, LPT1-9)        |
       |  - Alternate Data Streams (:stream, ::$DATA)                |
       |  - Trailing Dot / Space Strip Protection                    |
       |  - Drive Letters (C:) and UNC (\\server\share) Rejection    |
       |  - Control and Null Character Filtering                     |
       +-------------------------------------------------------------+
                                      |
                                      v
       +-------------------------------------------------------------+
       |               Reparse Point / Junction Defense              |
       |  - Handle-based resolution: GetFinalPathNameByHandleW       |
       |  - Symlink / Junction traversal detection                   |
       |  - Rejection of Volume Mount Points and Device Namespaces   |
       +-------------------------------------------------------------+
                                      |
                                      v
       +-------------------------------------------------------------+
       |            Enforced Sandbox Root: App Storage / VFS         |
       +-------------------------------------------------------------+
```

---

## 2. Windows-Specific Attack Vectors & Defense Mechanisms

### 2.1 Directory Traversal (`../`, `..\\`, Mixed Separators)
- **Threat:** Attackers include `../` or `..\` sequences to escape the extraction sandbox and overwrite critical host system files (e.g. `../../../../Windows/System32`).
- **Defense:**
  1. Lexical separator normalization: all `\` characters are mapped to `/`.
  2. Consecutive separators (`//`) are collapsed.
  3. Path segments are tokenized into an explicit stack.
  4. Segment `.` is discarded.
  5. Segment `..` pops from the stack; if the stack is empty (attempting to ascend beyond the root), the path is immediately rejected with `PathValidationStatus::DirectoryTraversal`.

### 2.2 Drive Letters, Device Namespaces, and UNC Paths
- **Threat:**
  - Absolute drive references (`C:\`, `D:\`) bypass relative bundle path constraints.
  - Device namespaces (`\\?\`, `\\.\`, `\??\`) bypass Win32 path normalization and length limits, allowing direct hardware or kernel device access.
  - UNC paths (`\\attacker-server\share\payload`) can trigger SMB network callbacks, credential leaks (NetNTLM), or remote payload execution.
- **Defense:**
  1. Any path containing a drive letter (`[a-zA-Z]:`) is rejected with `DriveLetterForbidden`.
  2. Paths beginning with `\\` or `//` are rejected with `UncPathForbidden`.
  3. Segments containing `\\?\` or `\??\` are rejected unconditionally.

### 2.3 Alternate Data Streams (ADS) (`:stream`, `::$DATA`)
- **Threat:** NTFS allows files to have named data streams attached via `:stream_name` (e.g., `payload.exe:hidden.dll` or `data.txt::$DATA`). Attackers can hide executable payloads, evade signature scanners, or corrupt file stream metadata.
- **Defense:**
  - Any path containing a colon (`:`) outside a forbidden drive letter is classified as an Alternate Data Stream and rejected with `AlternateDataStreamForbidden`.

### 2.4 Windows Reserved DOS Device Names
- **Threat:** Legacy MS-DOS device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1` through `COM9`, `LPT1` through `LPT9`) are reserved by the Win32 subsystem. Attempting to create or open a file named `CON`, `CON.txt`, `aux.json`, or `nul.dat` can hang the calling thread, crash the process, or interact with physical serial/parallel ports.
- **Defense:**
  - Every path segment is inspected: the stem before the first dot is converted to uppercase and compared against the reserved list.
  - Matching names are rejected with `WindowsReservedDevice`.

### 2.5 Trailing Periods and Trailing Spaces
- **Threat:** The Win32 subsystem automatically strips trailing dots and spaces from filenames (e.g., creating `secrets.txt.` or `app.exe ` results in `secrets.txt` or `app.exe`). Attackers exploit this to bypass blacklist filters or cause namespace collisions.
- **Defense:**
  - If any segment ends with a dot (`.`) or space (` `), the path is rejected with `TrailingDotOrSpace`.

### 2.6 Case Folding & 8.3 Short Filenames (SFN)
- **Threat:** NTFS is case-insensitive by default and generates 8.3 short aliases (`PROGRA~1` for `Program Files`). Attackers can reference files using short aliases to bypass case-sensitive path filters.
- **Defense:**
  - Case-folded comparisons and explicit rejection of short-name patterns containing tildes (`~`) where ambiguous.

### 2.7 Reparse Points, Directory Junctions, and Symlinks
- **Threat (SEC-232-03):** An attacker package might contain or create an NTFS junction (`IO_REPARSE_TAG_MOUNT_POINT`) or symbolic link (`IO_REPARSE_TAG_SYMLINK`) inside the app sandbox pointing to an external directory (e.g., `C:\Users\Target`). Lexical string prefix checking (`rfind(root) == 0`) sees the path as within the root, but the actual filesystem operation escapes to the external target.
- **Defense (Architecture for Stage 232.11):**
  1. Open a handle to the destination path using `CreateFileW(..., FILE_FLAG_BACKUP_SEMANTICS)`.
  2. Query the true physical canonical path using `GetFinalPathNameByHandleW(handle, ..., FILE_NAME_NORMALIZED | VOLUME_NAME_DOS)`.
  3. Check `FILE_ATTRIBUTE_REPARSE_POINT` on directory attributes.
  4. Compare the real target path against the sandbox root's canonical physical path. If the resolved path does not start with the sandbox root, access is denied.

---

## 3. Validation Matrix

| Attack Vector | Example Payload | Result / Status Code |
| :--- | :--- | :--- |
| Traversal (relative) | `../../windows/system32/cmd.exe` | `DirectoryTraversal` |
| Traversal (mixed) | `sub/..\\..\\secret.dat` | `DirectoryTraversal` |
| Drive letter | `C:/Windows/win.ini` | `DriveLetterForbidden` |
| UNC network | `\\192.168.1.1\share\file` | `UncPathForbidden` |
| Device namespace | `\\?\C:\secret` | `UncPathForbidden` / `DeviceForbidden` |
| Alternate Data Stream | `image.png:stream` | `AlternateDataStreamForbidden` |
| ADS Default Stream | `data.txt::$DATA` | `AlternateDataStreamForbidden` |
| Reserved Device | `CON`, `aux.txt`, `com1.log` | `WindowsReservedDevice` |
| Trailing Dot | `passwords.txt.` | `TrailingDotOrSpace` |
| Trailing Space | `executable.exe ` | `TrailingDotOrSpace` |
| Null character | `valid.txt\0malicious.bat` | `NullCharacterForbidden` |
| Control characters | `path\x01\x02name` | `ControlCharactersForbidden` |
| Depth exceeded | $>16$ nested subdirectories | `DepthExceeded` |
| Length exceeded | $>256$ characters | `PathTooLong` |
| Junction / Symlink | Link targeting `C:\Windows` | `EscapesSandboxRoot` |
