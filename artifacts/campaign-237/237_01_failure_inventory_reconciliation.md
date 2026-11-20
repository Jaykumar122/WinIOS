# Stage 237.1 — Failure Inventory Reconciliation

## 1. Executive Summary

This report reconciles the full test inventory of 988 registered CTest targets in WinIOS, accounting for all passing, skipped, and failing tests across `RelWithDebInfo` and `Debug` configurations.

At the Campaign 236 closeout:
- **Total Registered Targets:** 988
- **Skipped Targets:** 1 (#383 `inferno_integration_tests`)
- **Active Tests:** 987
- **RelWithDebInfo:** 947 Passed, 40 Failed (Pass Rate: 95.95%)
- **Debug:** 943 Passed, 44 Failed (Pass Rate: 95.54%)
- **Unique Failing Test Targets:** 44 distinct test targets across both configurations.

---

## 2. Inventory Accounting & Configuration Reconciliation

The discrepancy between 40 failures in `RelWithDebInfo` and 44 failures in `Debug` is accounted for by exactly **4 Debug-only failures**:
1. **Test #713 (`stage215_3_abi_hardening_tests`)**: Passes in RelWithDebInfo, fails/times out in Debug due to MSVC CRT assertion dialog handling.
2. **Test #722 (`stage215_22_security_fuzzing_tests`)**: Passes in RelWithDebInfo, fails/times out in Debug due to MSVC CRT assertion dialog handling.
3. **Test #805 (`stage222_15_timer_scheduler_tests`)**: Passes in RelWithDebInfo, fails/times out in Debug due to MSVC CRT assertion dialog handling.
4. **Test #855 (`stage226_7_keyboard_focus_tests`)**: Passes in RelWithDebInfo, fails/times out in Debug due to MSVC CRT assertion dialog handling.

All 40 failures observed in `RelWithDebInfo` are a strict subset of the 44 failures observed in `Debug`.

---

## 3. Exhaustive Roster of 44 Unique Failing Tests

| Test ID | Target Name | Origin Campaign | Primary Failing Subsystem | RelWithDebInfo Signature | Debug Signature |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **641** | `stage211_7_user_address_space_tests` | Campaign 211 | Memory Model (2GB limit) | Test failed (exit 1) | Test failed (exit 1) |
| **665** | `stage212_17_init_coordinator_tests` | Campaign 212 | Scheduler / Process Init | Test failed (exit 1) | Test failed (exit 1) |
| **666** | `stage212_18_multi_process_scheduler_tests` | Campaign 212 | Scheduler / TTBR0 EL1 | Line 74 TTBR0 mismatch | Line 74 TTBR0 mismatch |
| **668** | `stage212_20_multi_process_e2e_tests` | Campaign 212 | Multi-process Lifecycle | SEGFAULT (0xC0000005) | SEGFAULT (0xC0000005) |
| **670** | `stage212_22_adversarial_security_tests` | Campaign 212 | Security Boundaries | Test failed (exit 1) | Test failed (exit 1) |
| **687** | `stage213_20_guest_shell_tests` | Campaign 213 | Window Server / Shell | Test failed (exit 1) | Test failed (exit 1) |
| **688** | `stage213_21_interactive_apps_tests` | Campaign 213 | Window Server / Apps | Test failed (exit 1) | Test failed (exit 1) |
| **690** | `stage213_23_interactive_e2e_tests` | Campaign 213 | Window Server / E2E | Test failed (exit 1) | Test failed (exit 1) |
| **694** | `stage214_5_abi_boundary_tests` | Campaign 214 | ABI Version Freeze (v1.0) | ABI version 0x10000 mismatch | ABI version mismatch |
| **710** | `stage214_22_adversarial_security_tests` | Campaign 214 | Window Server Security | Test failed (exit 1) | Test failed (exit 1) |
| **711** | `stage214_23_desktop_e2e_tests` | Campaign 214 | Desktop Window Routing | Test failed (exit 1) | Test failed (exit 1) |
| **713** | `stage215_3_abi_hardening_tests` | Campaign 215 | ABI Version Freeze | Passed (NDEBUG) | CRT Assert Timeout / Abort |
| **722** | `stage215_22_security_fuzzing_tests` | Campaign 215 | Window Server Fuzzing | Passed (NDEBUG) | CRT Assert Timeout / Abort |
| **734** | `stage216_19_abi_v11_tests` | Campaign 216 | ABI Version Freeze (v1.1) | Exit 0xC0000409 (abort) | CRT Assert Timeout / Abort |
| **743** | `stage217_19_sdk_apps_tests` | Campaign 217 | ABI Version Freeze (v1.1) | Assert ABI 0x10100 | CRT Assert Timeout / Abort |
| **751** | `stage218_14_syscall_isolation_tests` | Campaign 218 | Syscall Isolation | Test failed (exit 1) | Test failed (exit 1) |
| **752** | `stage218_22_native_guest_e2e_tests` | Campaign 218 | Guest Application E2E | Test failed (exit 1) | Test failed (exit 1) |
| **753** | `stage218_23_adversarial_tests` | Campaign 218 | Syscall Boundary | Test failed (exit 1) | Test failed (exit 1) |
| **756** | `stage219_8_scheduler_execution_tests` | Campaign 219 | Scheduler / TTBR0 Switch | Assert TTBR0 == root_a | Assert TTBR0 == root_a |
| **757** | `stage219_10_synchronization_tests` | Campaign 219 | Guest Mutex Syscalls | Assert X0 == 16 | Assert X0 == 16 |
| **758** | `stage219_11_multi_thread_guest_workload_tests` | Campaign 219 | Multi-thread Workload | SEGFAULT (0xC0000005) | SEGFAULT (0xC0000005) |
| **765** | `stage219_22_multi_process_e2e_tests` | Campaign 219 | Multi-process Workload | Test failed (exit 1) | Test failed (exit 1) |
| **774** | `stage220_9_synchronization_tests` | Campaign 220 | Thread Sync | Test failed (exit 1) | Test failed (exit 1) |
| **775** | `stage220_10_thread_lifecycle_tests` | Campaign 220 | Thread Lifecycle | Test failed (exit 1) | Test failed (exit 1) |
| **777** | `stage220_13_syscall_hardening_tests` | Campaign 220 | Syscall Hardening | Test failed (exit 1) | Test failed (exit 1) |
| **779** | `stage220_15_pipe_ipc_tests` | Campaign 220 | VFS Pipe IPC | Test failed (exit 1) | Test failed (exit 1) |
| **780** | `stage220_16_msg_trap_tests` | Campaign 220 | Mach Message Traps | Test failed (exit 1) | Test failed (exit 1) |
| **801** | `stage222_7_event_queue_tests` | Campaign 222 | Event Queue Routing | Test failed (exit 1) | Test failed (exit 1) |
| **805** | `stage222_15_timer_scheduler_tests` | Campaign 222 | Timer Scheduler | Passed (NDEBUG) | CRT Assert Timeout / Abort |
| **808** | `stage222_20_interactive_notes_tests` | Campaign 222 | Native Notes App | Assert wm.window_count()==1 | Assert wm.window_count()==1 |
| **809** | `stage222_21_runtime_monitor_tests` | Campaign 222 | Native Monitor App | Assert wm.window_count()==1 | Assert wm.window_count()==1 |
| **819** | `stage223_19_calculator_tests` | Campaign 223 | Native Calculator App | Assert wm.window_count()==1 | Assert wm.window_count()==1 |
| **820** | `stage223_20_notes_v2_tests` | Campaign 223 | Native Notes v2 App | Assert wm.window_count()==1 | Assert wm.window_count()==1 |
| **821** | `stage223_21_resource_browser_tests` | Campaign 223 | Native Browser App | Assert wm.window_count()==1 | Assert wm.window_count()==1 |
| **834** | `stage224_22_platform_settings_tests` | Campaign 224 | Settings App | Assert wm.window_count()==1 | Assert wm.window_count()==1 |
| **835** | `stage224_22_ipc_messenger_tests` | Campaign 224 | Messenger App | Assert wm.window_count()==1 | Assert wm.window_count()==1 |
| **836** | `stage224_23_security_fuzzing_tests` | Campaign 224 | Desktop Fuzzing | Test failed (exit 1) | Test failed (exit 1) |
| **846** | `stage225_21_device_center_tests` | Campaign 225 | Device Center App | Assert wm.window_count()==1 | Assert wm.window_count()==1 |
| **847** | `stage225_21_system_monitor_tests` | Campaign 225 | System Monitor App | Assert wm.window_count()==1 | Assert wm.window_count()==1 |
| **848** | `stage225_22_multi_app_device_tests` | Campaign 225 | Multi-App Device App | Assert wm.window_count()==2 | Assert wm.window_count()==2 |
| **855** | `stage226_7_keyboard_focus_tests` | Campaign 226 | Keyboard Focus | Passed (NDEBUG) | CRT Assert Timeout / Abort |
| **870** | `stage227_23_adversarial_tests` | Campaign 227 | Document Syscalls | Test failed (exit 1) | Test failed (exit 1) |
| **908** | `stage231_6_canonical_path_tests` | Campaign 231 | Canonical Path Normalization | Test failed (exit 1) | Test failed (exit 1) |
| **912** | `stage231_10_ipc_token_security_tests` | Campaign 231 | IPC Generational Binding | Assert line 156 PermissionDenied | Assert line 156 PermissionDenied |

---

## 4. Investigation of Test #925 Discrepancy

In certain historical documentation from Campaign 232 and Campaign 233, an ambiguous boundary was described:
- Some summaries stated that Campaign 231 contributed 21 tests (#905–#925) and Campaign 232 contributed 15 tests (#926–#940).
- Other summaries stated that Campaign 232 contributed 16 tests (#925–#940) beginning with `stage232_4_asymmetric_signatures_tests`.

**Definitive Resolution via CMake Inspection:**
Inspection of `CMakeLists.txt` confirms:
- Campaign 231 defines targets from `stage231_2_security_primitives_tests` (#905) through `stage231_22_adversarial_suite_tests` (#924), totaling exactly **20 tests**.
- Test **#925** is registered as:
  ```cmake
  add_executable(stage232_4_asymmetric_signatures_tests tests/unit/stage232_4_asymmetric_signatures_tests.cpp)
  target_link_libraries(stage232_4_asymmetric_signatures_tests PRIVATE winios_core)
  add_test(NAME stage232_4_asymmetric_signatures_tests COMMAND stage232_4_asymmetric_signatures_tests)
  ```
- Therefore, **Test #925 belongs unambiguously to Campaign 232**.
- Campaign 232 spans Test **#925 through #940** (16 registered tests), all of which pass 100% in both configurations.
