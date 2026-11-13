# WINIOS CAMPAIGN 192 — STAGE 192.9 REPORT
## File Descriptor and VFS Semantics Conformance

**Campaign:** 192  
**Stage:** 192.9 (File Descriptor and VFS Semantics)  
**Date:** 2026-11-13  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Host Architecture:** Windows 11 x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Verdict:** **PASSED (5/5 test suites passed, 100% CTest regression clean, POSIX VFS semantics verified)**

---

## 1. Executive Summary

In Stage 192.9, the Darwin Virtual Filesystem (VFS) and process file descriptor table management (`DarwinVfs` and `DarwinBsdSubsystem`) were strengthened, audited, and verified against POSIX.1-2017 and Apple Darwin standards.

Key capabilities validated in Stage 192.9:
1. **Lowest-Available Descriptor Allocation & Reuse:** Upgraded `DarwinBsdSubsystem::open` and `DarwinBsdSubsystem::dup` to enforce POSIX descriptor reuse: when open descriptors are closed, creating a hole in the table, subsequent calls dynamically allocate the lowest unused file descriptor integer (`>= 3`).
2. **Access Mode Verification (O_RDONLY, O_WRONLY, O_RDWR):**
   - Enforced access flags: Attempts to write to an `O_RDONLY` (0x0000) descriptor fail with `EBADF` (`-1`).
   - Attempts to read from an `O_WRONLY` (0x0001) descriptor fail with `EBADF` (`-1`).
   - `O_RDWR` (0x0002) permits both operations.
3. **Append Mode Enforcement (`O_APPEND` = 0x0008):**
   - When opened with `O_APPEND`, every `write` operation automatically positions the file write offset to `node->content.size()` prior to writing, even if `lseek` had previously moved the cursor.
4. **Seek & Boundary Behavior (`lseek`):**
   - Verified `SEEK_SET` (0), `SEEK_CUR` (1), and `SEEK_END` (2) calculations.
   - Enforced negative offset rejection (`new_offset < 0` returns `-1`).
5. **EOF and Partial Reads/Writes:**
   - Reading at or beyond file size returns `0` (EOF).
   - Partial reads returning fewer bytes than requested when reaching file bounds.
6. **Pipe Lifecycle & Broken Pipe Semantics:**
   - Bidirectional pipe buffer FIFO ordering.
   - Closing write-end yields remaining buffered data, followed by clean EOF (`0`).
   - Closing read-end causes subsequent write attempts on the write-end to fail immediately with `EPIPE` (`-1`).
7. **Descriptor Duplication & Independent File Offsets:**
   - Validated `dup` (lowest free fd) and `dup2` (specific target fd, closing existing target occupant if open).
   - Verified that independent `open()` calls on the same file path maintain isolated file offsets.

---

## 2. Technical Implementation Details

### 2.1 Lowest-Available File Descriptor Reuse (`DarwinBsdSubsystem::open` & `dup`)
```cpp
int fd = 3;
while (proc->fds.find(fd) != proc->fds.end()) {
    fd++;
}
proc->fds[fd] = FileDescriptor{fd, node, nullptr, flags, 0};
return fd;
```

### 2.2 Access Mode & Append Check in `DarwinBsdSubsystem::read` and `write`
In `read`:
```cpp
if (!desc.is_pipe() && (desc.flags & 0x03u) == 1u) {
    return -1; // O_WRONLY cannot read
}
```
In `write`:
```cpp
if (!desc.is_pipe() && (desc.flags & 0x03u) == 0u) {
    return -1; // O_RDONLY cannot write
}
if (!desc.is_pipe() && (desc.flags & 0x0008u) != 0u && desc.node) {
    desc.offset = desc.node->content.size(); // O_APPEND
}
```

---

## 3. Test Verification Matrix

A dedicated test suite [`tests/unit/stage_192_9_vfs_tests.cpp`](file:///D:/WinISO26/WinIOS/tests/unit/stage_192_9_vfs_tests.cpp) was developed and registered as CMake target `stage_192_9_vfs_tests`:

| Test # | Focus Area | Description | Status |
|---|---|---|---|
| **Test 1** | Descriptor Allocation & Reuse | Validates lowest-available integer allocation, hole-filling reuse, and invalid descriptor rejection | **PASSED** |
| **Test 2** | Access Modes & O_APPEND | Validates read-only write failure, write-only read failure, and append-mode automatic offset pinning | **PASSED** |
| **Test 3** | Seek, EOF & Partial Transfer | Validates `lseek` whence modes, negative offset rejection, EOF detection, and partial reads | **PASSED** |
| **Test 4** | Pipe Lifecycle & EPIPE | Validates FIFO buffering, EOF upon write-end closure, and broken pipe error upon read-end closure | **PASSED** |
| **Test 5** | dup, dup2 & Isolated Offsets | Validates descriptor duplication, target fd reassignment, and offset isolation across independent opens | **PASSED** |

### Output:
```
=========================================================
STAGE 192.9: Darwin File Descriptor & VFS Semantics Tests
=========================================================
[Test 1] Descriptor Allocation & Lowest-Available Reuse...
[PASS] Descriptor Allocation & Lowest-Available Reuse
[Test 2] Access Modes (O_RDONLY, O_WRONLY, O_RDWR, O_APPEND)...
[PASS] Access Modes (O_RDONLY, O_WRONLY, O_RDWR, O_APPEND)
[Test 3] Seek, EOF, and Partial Read/Write...
[PASS] Seek, EOF, and Partial Read/Write
[Test 4] Pipe Lifecycle, EOF, and Broken Pipe (EPIPE)...
[PASS] Pipe Lifecycle, EOF, and Broken Pipe (EPIPE)
[Test 5] dup, dup2, and Independent File Descriptors...
[PASS] dup, dup2, and Independent File Descriptors

Results: 5 passed, 0 failed
```

---

## 4. Conformance & Regression Status
- MSVC C++20 build with `/W4 /WX /permissive-`: Clean (0 warnings, 0 errors).
- All prior stages (192.0 through 192.8) continue to pass.
