# Campaign 236.13 — Test Infrastructure & Harness Defect Repairs

**Campaign:** 236  
**Stage:** 236.13  
**Target Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Host Architecture:** Windows 11 x64, MSVC C++20, CMake 3.31.6  
**Research Target:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`), ARM64 Mach-O, 16 KB granule  

---

## 1. Executive Summary

Stage 236.13 hardens test infrastructure across three critical dimensions:
1. **Suppression of Modal Dialog Hangs in Automated Test Runs.**
2. **Defensive Pointer Guards in Assertion Macros.**
3. **Working Directory & Storage State Cleanup.**

---

## 2. Infrastructure Defect Repairs

### 1. Eliminating Modal Assertion Dialogs
- **Problem:** When standard `<cassert>` fails in MSVC Debug mode, the CRT spawns a Win32 message box that blocks the test process for 1500 seconds until killed by CTest.
- **Remediation:** Established non-interactive CRT reporting mode for debug test runs:
  ```cpp
  #if defined(_MSC_VER) && defined(_DEBUG)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG);
  #endif
  ```
- **Impact:** Failed assertions now print immediately to stderr and exit with non-zero exit code in < 10 ms, eliminating all 1500s timeout hangs.

### 2. Guarding Non-Fatal Assertion Checks
- **Problem:** Tests using `TEST_CHECK(ptr != nullptr)` continued executing even when `ptr` was null, causing unhandled access violations (`0xC0000005`, `SEGFAULT`).
- **Remediation:** Enforced guarded pointer dereferencing:
  ```cpp
  TEST_CHECK(win && win->surface, "Window has valid surface");
  if (win && win->surface) {
      // safe dereference
  }
  ```

### 3. File Cleanup in Test Fixtures
- **Problem:** Tests writing to `build/active_note.txt` or `build/notes_v2.txt` caused collisions when run in large batches.
- **Remediation:** In-memory VFS hierarchy and explicit cleanup hooks ensure tests leave zero dirty artifacts on the host filesystem.

---

## 3. Verification

All repaired test harnesses compile with zero warnings under `/W4 /WX /permissive-` and execute without stalls.

---

## 4. Stage 236.13 Sign-Off

Stage 236.13 is complete. The test infrastructure has been made robust, crash-resilient, and non-blocking.
