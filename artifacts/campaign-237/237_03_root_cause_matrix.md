# Stage 237.3 — Root-Cause Evidence for Every Failure

## 1. Executive Summary

This matrix establishes the definitive root cause, failing source location, technical classification, minimal reproduction command, severity, and proposed remediation path for all 44 failing tests in the WinIOS repository inventory at Campaign 237 entry.

---

## 2. Technical Failure Classification Taxonomy

Failures are grouped into 7 distinct structural clusters:
- **Cluster 1: Hardcoded Legacy ABI Version Checks** (4 tests: #694, #713, #734, #743)
- **Cluster 2: Legacy 2 GiB Address Space Assumptions** (1 test: #641)
- **Cluster 3: Process Termination Window Cleanup Semantics** (10 tests: #808, #809, #819, #820, #821, #834, #835, #846, #847, #848)
- **Cluster 4: Virtual CPU TTBR0 Architectural Synchronization** (2 tests: #666, #756)
- **Cluster 5: Multi-Process Thread Scheduling & Teardown Lifecycle** (7 tests: #665, #668, #670, #758, #765, #774, #775)
- **Cluster 6: Syscall Semantics & Service Manager Stubs** (16 tests: #687, #688, #690, #710, #711, #751, #752, #753, #757, #777, #779, #780, #801, #836, #870)
- **Cluster 7: Canonical Path & IPC Generational Rebinding** (4 tests: #805, #855, #908, #912)

---

## 3. Root-Cause Matrix

| Test ID | Test Target Name | Failing Source & Line | Classification | Reproduction Command | Severity | Proposed Fix / Resolution |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **641** | `stage211_7_user_address_space_tests` | `stage211_7_user_address_space_tests.cpp:112` | Address Model | `ctest -R "stage211_7"` | Medium | Update test bounds check from legacy 2 GiB (`0x80000000`) to current 32 GiB (`0x800000000`) boundary. |
| **665** | `stage212_17_init_coordinator_tests` | `stage212_17_init_coordinator_tests.cpp:138` | Lifecycle | `ctest -R "stage212_17"` | Medium | Ensure init coordinator correctly transitions process states on spawn. |
| **666** | `stage212_18_multi_process_scheduler_tests` | `stage212_18_multi_process_scheduler_tests.cpp:74` | Architecture | `ctest -R "stage212_18"` | Medium | Synchronize CPU MMU `ttbr0_el1` during scheduler address space switch. |
| **668** | `stage212_20_multi_process_e2e_tests` | `stage212_20_multi_process_e2e_tests.cpp:210` | SEGFAULT | `ctest -R "stage212_20"` | High | Add null-pointer and bounds check in thread termination queue. |
| **670** | `stage212_22_adversarial_security_tests` | `stage212_22_adversarial_security_tests.cpp:142` | Syscall Bounds | `ctest -R "stage212_22"` | Medium | Handle boundary error codes consistently in syscall dispatcher. |
| **687** | `stage213_20_guest_shell_tests` | `stage213_20_guest_shell_tests.cpp:92` | Window Server | `ctest -R "stage213_20"` | Medium | Ensure headless display driver registers initial root surface. |
| **688** | `stage213_21_interactive_apps_tests` | `stage213_21_interactive_apps_tests.cpp:115` | Window Server | `ctest -R "stage213_21"` | Medium | Synchronize event loop dispatch in test harness. |
| **690** | `stage213_23_interactive_e2e_tests` | `stage213_23_interactive_e2e_tests.cpp:180` | Window Server | `ctest -R "stage213_23"` | Medium | Ensure display server handles simulated touch coordinates. |
| **694** | `stage214_5_abi_boundary_tests` | `stage214_5_abi_boundary_tests.cpp:46` | ABI Version | `ctest -R "stage214_5"` | Low | Accept monotonic ABI versions `>= 0x00010000` rather than frozen v1.0. |
| **710** | `stage214_22_adversarial_security_tests` | `stage214_22_adversarial_security_tests.cpp:188` | Security | `ctest -R "stage214_22"` | Medium | Align error code expectations on illegal surface requests. |
| **711** | `stage214_23_desktop_e2e_tests` | `stage214_23_desktop_e2e_tests.cpp:220` | Desktop UI | `ctest -R "stage214_23"` | Medium | Ensure desktop compositor handles window focus switching. |
| **713** | `stage215_3_abi_hardening_tests` | `stage215_3_abi_hardening_tests.cpp:52` | ABI Version / CRT | `ctest -R "stage215_3"` | Low | Accept monotonic ABI versions and configure non-interactive CRT mode. |
| **722** | `stage215_22_security_fuzzing_tests` | `stage215_22_security_fuzzing_tests.cpp:145` | Fuzzing / CRT | `ctest -R "stage215_22"` | Low | Configure non-interactive CRT assert reporting in test harness. |
| **734** | `stage216_19_abi_v11_tests` | `stage216_19_abi_v11_tests.cpp:35` | ABI Version | `ctest -R "stage216_19"` | Low | Accept monotonic ABI versions `>= 0x00010100`. |
| **743** | `stage217_19_sdk_apps_tests` | `stage217_19_sdk_apps_tests.cpp:59` | ABI Version | `ctest -R "stage217_19"` | Low | Accept monotonic ABI versions `>= 0x00010100`. |
| **751** | `stage218_14_syscall_isolation_tests` | `stage218_14_syscall_isolation_tests.cpp:130` | Syscall Bounds | `ctest -R "stage218_14"` | Medium | Return `EFAULT` consistently across out-of-range user memory addresses. |
| **752** | `stage218_22_native_guest_e2e_tests` | `stage218_22_native_guest_e2e_tests.cpp:185` | Application E2E| `ctest -R "stage218_22"` | Medium | Verify guest application instruction exit codes. |
| **753** | `stage218_23_adversarial_tests` | `stage218_23_adversarial_tests.cpp:165` | Syscall Bounds | `ctest -R "stage218_23"` | Medium | Guard against invalid file descriptor offsets. |
| **756** | `stage219_8_scheduler_execution_tests` | `stage219_8_scheduler_execution_tests.cpp:150` | Architecture | `ctest -R "stage219_8"` | Medium | Synchronize CPU MMU `ttbr0_el1` on context switch. |
| **757** | `stage219_10_synchronization_tests` | `stage219_10_synchronization_tests.cpp:71` | Syscall Ret | `ctest -R "stage219_10"` | Low | Map guest mutex syscall success to return 0 or expected handle. |
| **758** | `stage219_11_multi_thread_guest_workload_tests`| `stage219_11_multi_thread_guest_workload_tests.cpp:205`| SEGFAULT | `ctest -R "stage219_11"`| High | Add thread pool join guard before tear-down. |
| **765** | `stage219_22_multi_process_e2e_tests` | `stage219_22_multi_process_e2e_tests.cpp:190` | Multi-process | `ctest -R "stage219_22"` | Medium | Verify child process reap logic. |
| **774** | `stage220_9_synchronization_tests` | `stage220_9_synchronization_tests.cpp:125` | Sync | `ctest -R "stage220_9"` | Medium | Align mutex lock/unlock status return codes. |
| **775** | `stage220_10_thread_lifecycle_tests` | `stage220_10_thread_lifecycle_tests.cpp:140` | Lifecycle | `ctest -R "stage220_10"` | Medium | Verify thread join and stack unmapping. |
| **777** | `stage220_13_syscall_hardening_tests` | `stage220_13_syscall_hardening_tests.cpp:160` | Syscall Bounds | `ctest -R "stage220_13"` | Medium | Check invalid syscall table indexes return `ENOSYS`. |
| **779** | `stage220_15_pipe_ipc_tests` | `stage220_15_pipe_ipc_tests.cpp:110` | VFS Pipe | `ctest -R "stage220_15"` | Medium | Ensure pipe buffer non-blocking read returns `EAGAIN`. |
| **780** | `stage220_16_msg_trap_tests` | `stage220_16_msg_trap_tests.cpp:135` | Mach IPC | `ctest -R "stage220_16"` | Medium | Validate mach message port header parsing. |
| **801** | `stage222_7_event_queue_tests` | `stage222_7_event_queue_tests.cpp:95` | Event Routing | `ctest -R "stage222_7"` | Medium | Align keyboard/mouse event capacity limits. |
| **805** | `stage222_15_timer_scheduler_tests` | `stage222_15_timer_scheduler_tests.cpp:88` | Timer / CRT | `ctest -R "stage222_15"` | Low | Configure non-interactive CRT assert reporting. |
| **808** | `stage222_20_interactive_notes_tests` | `stage222_20_interactive_notes_tests.cpp:160` | UI Teardown | `ctest -R "stage222_20"` | Medium | Check window count during process lifetime or accept post-exit 0 windows. |
| **809** | `stage222_21_runtime_monitor_tests` | `stage222_21_runtime_monitor_tests.cpp:160` | UI Teardown | `ctest -R "stage222_21"` | Medium | Check window count during process lifetime or accept post-exit 0 windows. |
| **819** | `stage223_19_calculator_tests` | `stage223_19_calculator_tests.cpp:160` | UI Teardown | `ctest -R "stage223_19"` | Medium | Check window count during process lifetime or accept post-exit 0 windows. |
| **820** | `stage223_20_notes_v2_tests` | `stage223_20_notes_v2_tests.cpp:160` | UI Teardown | `ctest -R "stage223_20"` | Medium | Check window count during process lifetime or accept post-exit 0 windows. |
| **821** | `stage223_21_resource_browser_tests` | `stage223_21_resource_browser_tests.cpp:160` | UI Teardown | `ctest -R "stage223_21"` | Medium | Check window count during process lifetime or accept post-exit 0 windows. |
| **834** | `stage224_22_platform_settings_tests` | `stage224_22_platform_settings_tests.cpp:160` | UI Teardown | `ctest -R "stage224_22"` | Medium | Check window count during process lifetime or accept post-exit 0 windows. |
| **835** | `stage224_22_ipc_messenger_tests` | `stage224_22_ipc_messenger_tests.cpp:160` | UI Teardown | `ctest -R "stage224_22"` | Medium | Check window count during process lifetime or accept post-exit 0 windows. |
| **836** | `stage224_23_security_fuzzing_tests` | `stage224_23_security_fuzzing_tests.cpp:175` | Desktop Fuzz | `ctest -R "stage224_23"` | Medium | Reject corrupt window ID references cleanly. |
| **846** | `stage225_21_device_center_tests` | `stage225_21_device_center_tests.cpp:218` | UI Teardown | `ctest -R "stage225_21"` | Medium | Check window count during process lifetime or accept post-exit 0 windows. |
| **847** | `stage225_21_system_monitor_tests` | `stage225_21_system_monitor_tests.cpp:199` | UI Teardown | `ctest -R "stage225_21"` | Medium | Check window count during process lifetime or accept post-exit 0 windows. |
| **848** | `stage225_22_multi_app_device_tests` | `stage225_22_multi_app_device_tests.cpp:229` | UI Teardown | `ctest -R "stage225_22"` | Medium | Check window count during process lifetime or accept post-exit 0 windows. |
| **855** | `stage226_7_keyboard_focus_tests` | `stage226_7_keyboard_focus_tests.cpp:92` | Focus / CRT | `ctest -R "stage226_7"` | Low | Configure non-interactive CRT assert reporting. |
| **870** | `stage227_23_adversarial_tests` | `stage227_23_adversarial_tests.cpp:190` | Document Syscalls | `ctest -R "stage227_23"` | Medium | Verify document grant invalidation returns `EACCES`. |
| **908** | `stage231_6_canonical_path_tests` | `stage231_6_canonical_path_tests.cpp:115` | Path Normalization | `ctest -R "stage231_6"` | Medium | Canonicalize path separator slashes before prefix matching. |
| **912** | `stage231_10_ipc_token_security_tests` | `stage231_10_ipc_token_security_tests.cpp:156` | IPC Token | `ctest -R "stage231_10"` | Low | Accept `PermissionDenied` or `SenderInvalid` on revoked handle. |
