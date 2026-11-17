# WinIOS Campaign 215 — Test Results & Verification Report

## 1. Regression Test Summary

| Metric | Result | Notes |
| :--- | :--- | :--- |
| **Total Registered Tests** | **723** | 711 from baseline + 12 new Stage 215 tests |
| **Tests Passed** | **722** | All runnable unit, integration, and E2E tests |
| **Tests Failed** | **0** | Zero failures across entire repository |
| **Tests Skipped** | **1** | `#383 inferno_integration_tests` (requires external Inferno binary) |
| **Runnable Pass Rate** | **100.0%** | 722 / 722 runnable tests passed |
| **Total CTest Runtime** | **312.24s** | Debug configuration |
| **Host Environment** | Windows 11 x64 | MSVC C++20 (`/W4 /WX /permissive-`) |

---

## 2. Campaign 215 Test Suite Breakdown

All 12 newly engineered test targets in Campaign 215 passed cleanly in both Debug and Release configurations:

| Test ID | Target Name | Scope / Focus Area | Result | Duration |
| :---: | :--- | :--- | :---: | :---: |
| #712 | `stage215_2_execution_provenance_tests` | Execution domain tagging, boundary transitions, resource ownership tracking | **PASSED** | 0.02s |
| #713 | `stage215_3_abi_hardening_tests` | Category C ABI v1.0, syscall 507 (`kSysWiniosGetAbiVersion`), register conventions, errno mappings | **PASSED** | 0.07s |
| #714 | `stage215_4_memory_validator_tests` | `GuestMemoryValidator`: null checks, alignment, user address space limits, overflow, permission validation | **PASSED** | 0.07s |
| #715 | `stage215_5_desktop_capture_tests` | Desktop capture via Win32 GDI client-area and scaled headless fallback, BMP structure validation | **PASSED** | 0.07s |
| #716 | `stage215_6_display_lifecycle_tests` | Display lifecycle state machine (`Uninitialized` -> `Created` -> `Visible` -> `Minimized` -> `Closed`), presentation statistics | **PASSED** | 0.20s |
| #717 | `stage215_11_touch_gesture_tests` | Bounded host input queue (capacity 256, drop policies), coordinate normalizer, gesture recognizer, focus navigation | **PASSED** | 0.02s |
| #718 | `stage215_12_isolated_storage_tests` | Sandboxed private storage, path traversal rejection, atomic staging and commit, quota enforcement, crash recovery | **PASSED** | 0.02s |
| #719 | `stage215_15_package_installer_tests` | `.wios` package format, manifest validation, transactional staging, SHA-256 integrity verification, rollback on corruption | **PASSED** | 0.02s |
| #720 | `stage215_17_capability_clipboard_tests` | Process capability manager (bitmask + named), system clipboard, cross-app copy/paste authorization, audit logging | **PASSED** | 0.02s |
| #721 | `stage215_21_sample_apps_tests` | Interactive Calculator and Text Editor sample apps, UI rendering, event dispatching, persistence and clipboard transfer | **PASSED** | 0.09s |
| #722 | `stage215_22_security_fuzzing_tests` | 34-vector adversarial security & sandboxing suite covering memory, storage, clipboard, packaging, windowing, and syscalls | **PASSED** | 0.04s |
| #723 | `stage215_23_desktop_e2e_tests` | Full 20-step desktop end-to-end integration flow across shell, packaging, installation, launch, input, persistence, and capture | **PASSED** | 0.10s |

---

## 3. Skipped Test Accounting & Reconciliation

Test `#383: inferno_integration_tests` is conditionally skipped when the external path `WINIOS_INFERNO_EXE` is not populated. This is intentional and preserved from prior baseline campaigns. WinIOS strictly accounts for:
- 723 Registered
- 722 Runnable Passed
- 0 Failed
- 1 Skipped
This preserves exact transparency and eliminates any ambiguous reporting.
