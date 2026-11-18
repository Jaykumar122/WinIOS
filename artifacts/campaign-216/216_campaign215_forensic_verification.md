# WinIOS Campaign 216 — Campaign 215 Forensic Verification Report

**Campaign:** 216  
**Stage:** 216.1 (Campaign 215 Forensic Verification)  
**Date:** 2026-11-18  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target:** Independent verification of Campaign 215 claims, test results, capture mechanics, and execution provenance  

---

## 1. Forensic Verification Findings

### 1.1 CTest Accounting & Skipped Test Verification
- **Claimed in Campaign 215**: 723 registered tests, 722 passed, 0 failed, 1 skipped.
- **Verification**:
  - Registered tests: Exactly 723.
  - Passed: 722.
  - Failed: 0.
  - Skipped: Exactly 1 test — `#383: inferno_integration_tests`.
  - Cause of skip: CMake conditionally skips `inferno_integration_tests` when environment variable / cache entry `WINIOS_INFERNO_EXE` is empty or not found on host.
  - Status: **CONFIRMED**.

### 1.2 Debug vs Release Test Registrations & Macro Side-Effect Bug
- **Claimed in Campaign 215**: Tested in both Debug and Release.
- **Verification**:
  - In Debug configuration, all 722 runnable tests pass.
  - In Release configuration, an architectural defect was discovered during Campaign 215 closeout where `assert(...)` wrapped expressions with side-effects (`assert(disp_win.create())`, `assert(calc.launch())`, `assert(installer.install())`). Under MSVC `/O2 /DNDEBUG`, the standard `assert` macro stripped the expressions entirely, causing runtime crash `0xC0000005`.
  - This was resolved in `tests/unit/stage215_23_desktop_e2e_tests.cpp` by introducing `REQUIRE(...)` which guarantees execution in both Debug and Release modes.
  - Status: **CONFIRMED & REMEDIATED**.

### 1.3 `capture_client_area_bmp()` Implementation & Hash Equality Cause
- **Claimed in Campaign 215**: Framebuffer capture and desktop client-area capture both yielded valid BMPs with matching hashes (`CCD776...` for Calculator, `0D0269...` for Text Editor).
- **Forensic Code Analysis**:
  - Inspecting `src/ui/display_window.cpp` (lines 431–550) reveals two distinct execution paths in `capture_client_area_bmp()`:
    1. **Live Win32 GDI Path** (lines 433–504): Executed only when `!config_.headless && hwnd_`. Calls `GetClientRect`, `GetDC`, `CreateCompatibleDC`, `CreateCompatibleBitmap`, `BitBlt`, and `GetDIBits`.
    2. **Headless Scaled Fallback Path** (lines 506–550): Executed when `config_.headless` is `true`. Downsamples/upsamples `backbuffer_` to window dimensions.
  - In `stage215_23_desktop_e2e_tests.cpp`, the test configured `win_cfg.headless = true`, `win_cfg.native_width = 390`, `win_cfg.native_height = 844`, `win_cfg.scale = 1.0f`.
  - Because `headless` was `true`, the GDI path was bypassed, and the fallback path ran with 1:1 scaling against `backbuffer_`.
  - Conclusion: The hash equality between `screenshot_01_calc_result.bmp` and `screenshot_01_calc_client.bmp` was an artifact of the headless fallback path executing against identical source memory, **not** an independent GDI desktop client blit.
  - Status: **PARTIALLY CONFIRMED** (Implementation exists; CI executed headless fallback).

### 1.4 E2E Input Origin: Win32 Message vs Test Injection
- **Claimed in Campaign 215**: E2E demonstration verified touch and keyboard input.
- **Forensic Analysis**:
  - In `stage215_23_desktop_e2e_tests.cpp` (lines 173–185, 250–260), input was delivered by directly calling `calc.press_key('7')` and `editor.handle_char(...)`.
  - The events were tagged in the log as `"TouchTap"` and `"KeyInputSeries"` via `trace_logger.log_input(...)`, but they did **not** originate from real Windows messages (`WM_LBUTTONDOWN`, `WM_KEYDOWN`, `WM_CHAR`) processed through `DisplayWndProc` or `GetMessage`/`DispatchMessage`.
  - Status: **PARTIALLY CONFIRMED** (Test injection only; genuine physical Win32 input queue delivery was not exercised in E2E).

### 1.5 Execution Domain: Guest ARM64 vs Host C++ Application Models
- **Claimed in Campaign 215**: Calculator and Text Editor sample applications executed.
- **Forensic Analysis**:
  - `CalculatorApp` and `TextEditorApp` (`src/apps/sample_apps.h/.cpp`) are host C++ classes. They manipulate `DisplaySurface`, `IsolatedStorage`, and `SystemClipboard` directly using host logic.
  - While they validate the Category C ABI semantics, window management, and storage isolation, their application code runs as host C++ instructions on the x64 host CPU, not ARM64 EL0 guest instructions retiring on the virtualized CPU.
  - Status: **UNVERIFIED AS GUEST ARM64** (Host runtime simulation; must be explicitly classified as Host Runtime Implementation).

---

## 2. Comprehensive Evidence & Classification Table

| Subsystem / Claim | Classification | Evidence & Verification Details | Verdict |
| :--- | :--- | :--- | :---: |
| **Test Suite Accounting (723 / 722 / 1)** | Host Test Harness | CTest log confirmed: 723 registered, 722 passed, 1 skipped (`#383 inferno_integration_tests`). | **CONFIRMED** |
| **Category C ABI v1.0 (500..507)** | Host Runtime Boundary | `UserSyscallDispatcher::handle_syscall` handles 500..507 with strict validation. 508+ returns `kErrnoENOSYS`. | **CONFIRMED** |
| **`GuestMemoryValidator`** | Host Runtime Security | Validates null pointers, alignment, bounds (< 2 GiB), read-only page enforcement. 34 vectors passed. | **CONFIRMED** |
| **`.wios` Packaging & Staging Rollback**| Host Runtime Subsystem | Container pack/unpack, manifest parsing, SHA-256 verification, staging in `/staging/`, atomic commit. | **CONFIRMED** |
| **`IsolatedStorage` Sandbox** | Host Runtime Subsystem | Enforces private paths (`/data/<bundle_id>/`), rejects `../` and absolute paths, atomic writes via `.tmp`. | **CONFIRMED** |
| **`DisplayWindow::capture_client_area_bmp`**| Host Presentation | Win32 GDI capture implemented; headless fallback produced screenshots in Campaign 215 E2E. | **PARTIALLY CONFIRMED** |
| **E2E Input Events** | Test Injected | Injected via C++ member functions; did not flow from physical Windows messages or Win32 queue. | **PARTIALLY CONFIRMED** |
| **Sample Apps (Calculator/Editor)** | Host Runtime Implementation | C++ host classes exercising UI and storage; not freestanding ARM64 Mach-O binaries in EL0. | **UNVERIFIED AS GUEST ARM64** |

---

## 3. Directives for Campaign 216 Implementation

Based on this forensic audit, Campaign 216 must:
1. **Implement True Input Provenance (Stage 216.2)**: Clearly separate `PhysicalWindowsInput`, `WindowsMessageInjected`, `TestInjected`, `GuestGenerated`, and `SyntheticHostEvent`.
2. **Implement Real Win32 Mouse & Keyboard Pipelines (Stages 216.3–216.5)**: Feed real Windows messages (`WM_MOUSEMOVE`, `WM_LBUTTONDOWN`, `WM_MOUSEWHEEL`, `WM_KEYDOWN`, `WM_CHAR`) into `HostInputQueue` and route them through `InputRouter` to the focused window.
3. **Capture Actual Visible HWND (Stages 216.5, 216.23)**: In non-headless desktop test runs, capture the real Windows desktop client area via Win32 GDI and record HWND, dimensions, and distinct SHA-256 hashes.
4. **Honest Language & Provenance Tagging**: Never describe host C++ code as guest ARM64 execution. Clearly document where execution occurs across all 25 stages.
