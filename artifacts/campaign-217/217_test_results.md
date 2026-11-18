# WINIOS CAMPAIGN 217 — VERIFIED TEST EXECUTION RESULTS

**Date:** November 18, 2026  
**Host:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Baseline Test Count:** 737 registered test suites (Campaign 216)  
**New Campaign 217 Test Suites:** 7 registered test suites  
**Total Test Count:** 744 registered test suites  
**Execution Environment:** CTest, Debug & Release configurations

---

## 1. Campaign 217 Dedicated Test Suites

All 7 test suites pass 100% in both Debug and Release builds with zero warnings and zero failures.

| Test # | Test Suite Name | Focus Area | Result | Debug Duration | Release Duration |
|---|---|---|---|---|---|
| 738 | `stage217_2_contract_tests` | ABI v1.2, X0 Arg Block, SP Alignment, Exit Descriptor | PASSED | 0.02s | 0.01s |
| 739 | `stage217_5_allocator_tests` | GuestAllocator, Quota Enforcement, In-Place Realloc, Leak Detection, mini_crt | PASSED | 0.02s | 0.01s |
| 740 | `stage217_7_storage_tests` | GuestStorageApi, Isolated Storage Sandbox, Quotas, Async I/O Manager | PASSED | 0.02s | 0.01s |
| 741 | `stage217_8_event_ipc_tests` | GuestEventLoop, Provenance Preservation, Timer Ticks, GuestIpcRouter Bounded Mailbox | PASSED | 0.03s | 0.03s |
| 742 | `stage217_12_resources_ui_tests` | ResourceBundle, ResourceManager, BMP Decoder, FontEngine, GuestUI Widgets & Layout | PASSED | 0.02s | 0.01s |
| 743 | `stage217_19_sdk_apps_tests` | Developer SDK v1.2, Runtime Inspector, Resource Viewer, Task Notes Apps & Packaging | PASSED | 0.03s | 0.01s |
| 744 | `stage217_23_developer_e2e_tests` | 20-Step Verified Developer Workflow Scenario & Adversarial Fuzzing | PASSED | 0.11s | 0.03s |

---

## 2. Regression Suites (Campaign 216 & 215)

Regression test suites were executed under Release configuration to guarantee zero regressions.

### Campaign 216 Regression Suites (14 of 14 Passed)
- `stage216_2_input_provenance_tests`: PASSED (0.03s)
- `stage216_3_mouse_input_tests`: PASSED (0.03s)
- `stage216_4_keyboard_input_tests`: PASSED (0.03s)
- `stage216_5_focus_capture_tests`: PASSED (0.03s)
- `stage216_6_input_security_tests`: PASSED (0.04s)
- `stage216_7_multi_window_tests`: PASSED (0.04s)
- `stage216_8_decorations_hit_test_tests`: PASSED (0.03s)
- `stage216_11_multi_window_compositor_damage_tests`: PASSED (0.03s)
- `stage216_13_window_animator_tests`: PASSED (0.03s)
- `stage216_14_renderer_tests`: PASSED (0.04s)
- `stage216_19_abi_v11_tests`: PASSED (0.04s)
- `stage216_21_multi_window_app_tests`: PASSED (0.05s)
- `stage216_22_guest_event_services_tests`: PASSED (0.03s)
- `stage216_23_desktop_e2e_tests`: PASSED (0.22s)

### Campaign 215 Regression Suites (12 of 12 Passed)
- `stage215_2_execution_provenance_tests`: PASSED (0.05s)
- `stage215_3_abi_hardening_tests`: PASSED (0.07s)
- `stage215_4_memory_validator_tests`: PASSED (0.06s)
- `stage215_5_desktop_capture_tests`: PASSED (0.04s)
- `stage215_6_display_lifecycle_tests`: PASSED (0.09s)
- `stage215_11_touch_gesture_tests`: PASSED (0.04s)
- `stage215_12_isolated_storage_tests`: PASSED (0.04s)
- `stage215_15_package_installer_tests`: PASSED (0.04s)
- `stage215_17_capability_clipboard_tests`: PASSED (0.04s)
- `stage215_21_sample_apps_tests`: PASSED (0.04s)
- `stage215_22_security_fuzzing_tests`: PASSED (0.05s)
- `stage215_23_desktop_e2e_tests`: PASSED (0.09s)

---

## 3. Toolchain & Command-Line Validation

1. **Zig 0.17.0 Mach-O Compilation:**
   - Compiled freestanding ARM64 C source with `tools/wpack.py compile`.
   - Verified 64-bit Mach-O magic (`0xFEEDFACF`) and ARM64 CPU type (`0x0100000C`).
   - Validated entry point and load commands.
2. **Compiler Warning Enforcement:**
   - Compiled with `/W4 /WX /permissive-`.
   - Result: 0 compiler warnings, 0 linker warnings across all core files and test suites.
