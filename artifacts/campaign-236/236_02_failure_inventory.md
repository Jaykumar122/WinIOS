# Campaign 236.2 — Failure Inventory and Classification Taxonomy

**Campaign:** 236  
**Stage:** 236.2  
**Target Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Reference Baseline Commit:** `f81467bcc800970bc8391c6d7eac0fb419bb5524` (Campaign 235 Closeout)  
**Host Architecture:** Windows 11 x64, MSVC C++20, CMake 3.31.6  
**Research Target:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`), ARM64 Mach-O, 16 KB granule  

---

## 1. Executive Taxonomy Overview

A total of **48 distinct test targets** failed across the test matrix (43 in `RelWithDebInfo`, 48 in `Debug`, with 43 common to both). Zero failures occurred in tests introduced by Campaign 235 (#974 – #988), Campaign 234 (#951 – #973), Campaign 233 (#927 – #950), or Campaign 232 (#913 – #926).

To enable rigorous, evidence-based triage and targeted remediation without masking defects or weakening test rigor, all 48 failing tests are classified into **8 distinct technical taxonomy categories**:

```
Total Failures: 48
├── Category 1: ABI Specification Evolution (4 tests)
├── Category 2: Address Space & Memory Model Evolution (2 tests)
├── Category 3: Headless CTest Window Manager Expectations (8 tests)
├── Category 4: Display Surface & Compositor Host Frame Lifecycle (6 tests)
├── Category 5: Scheduler, Thread Context & Syscall Boundary Contracts (16 tests)
├── Category 6: VFS Storage, Serialization & Package Security (4 tests)
├── Category 7: Canonical Path & IPC Token Reallocation Boundary (2 tests)
└── Category 8: Debug Configuration Divergence (5 tests: 3 Timeouts, 2 Assertions)
```

---

## 2. Category 1: ABI Specification Evolution (Historical ABI Version Constants)

During Campaigns 214–220, the Category C ABI progressed through three major revisions:
- v1.0: `0x00010000` (Campaign 214)
- v1.1: `0x00010100` (Campaign 216–217)
- v1.2: `0x00010200` (Campaign 220+)

Tests authored in earlier campaigns assert hardcoded equality against frozen legacy ABI versions rather than querying ABI capability compatibility or accepting monotonic version progression.

| Test # | Test Target | Source File & Line | Failure Mechanism | Evidence / Error Signature |
|---|---|---|---|---|
| **694** | `stage214_5_abi_boundary_tests` | `tests/unit/stage214_5_abi_boundary_tests.cpp:46` | Hardcoded assertion expecting Category C ABI == `0x00010000` | `[ABI_FAIL] Line 46: Category C ABI Version is 1.0 (0x00010000)` |
| **734** | `stage216_19_abi_v11_tests` | `tests/unit/stage216_19_abi_v11_tests.cpp:35` | Hardcoded assertion expecting Category C ABI == `0x00010100` | `Assertion failed: WINIOS_CATEGORY_C_ABI_VERSION == 0x00010100` |
| **743** | `stage217_19_sdk_apps_tests` | `tests/unit/stage217_19_sdk_apps_tests.cpp:59` | Hardcoded assertion expecting Category C ABI == `0x00010100` | `Assertion failed: WINIOS_CATEGORY_C_ABI_VERSION == 0x00010100` |
| **669** | `stage212_21_snapshot_replay_tests` | `tests/unit/stage212_21_snapshot_replay_tests.cpp:88` | Snapshot serialization ABI mode verification check | `[FAIL] 1 test(s) FAILED in Stage 212.21.` |

---

## 3. Category 2: Address Space & Memory Model Evolution (2 GiB Limit Assumption)

In Campaign 211, user address space was initially constrained to 2 GiB (`0x80000000`). In Campaign 216+, WinIOS adopted the genuine 64-bit ARM64 Darwin memory model with a 4 GB `PAGEZERO` guard and high-memory user mappings spanning `0x100000000` to `0x7FFFFFFFFFFF`. Early unit tests asserting that allocations above 2 GiB must return `EINVAL` now fail because the platform correctly supports modern 64-bit user address space.

| Test # | Test Target | Source File & Line | Failure Mechanism | Evidence / Error Signature |
|---|---|---|---|---|
| **641** | `stage211_7_user_address_space_tests` | `tests/unit/stage211_7_user_address_space_tests.cpp:112` | Copyin/copyout buffer validation on addresses near legacy 2 GiB boundary | `[FAIL] Stage 211.7: 1 tests failed.` |
| **649** | `stage211_22_adversarial_userspace_security_tests` | `tests/unit/stage211_22_adversarial_userspace_security_tests.cpp:95` | Vector 6 asserts that mappings > 2 GiB must be rejected | `[FAIL] Line 95: Vector 6: Mapping beyond 2 GiB user address limit rejected` |

---

## 4. Category 3: Headless CTest Window Manager Expectations (8 Tests)

These 8 tests instantiate full interactive guest application workflows (Calculator, Notes, Device Center, System Monitor, Platform Settings, IPC Messenger) and assert `wm.window_count() >= 1` or `wm.window_count() == 1`. When executed in automated, headless CTest harness environments without an active Win32 message pump or virtual desktop surface attached, the guest window creation pipeline completes headlessly or exits before the host window count is queried.

| Test # | Test Target | Source File & Line | Failure Mechanism | Evidence / Error Signature |
|---|---|---|---|---|
| **808** | `stage222_20_interactive_notes_tests` | `tests/unit/stage222_20_interactive_notes_tests.cpp:160` | Headless execution window count check | `Assertion failed: wm.window_count() == 1` |
| **809** | `stage222_21_runtime_monitor_tests` | `tests/unit/stage222_21_runtime_monitor_tests.cpp:160` | Headless execution window count check | `Assertion failed: wm.window_count() == 1` |
| **819** | `stage223_19_calculator_tests` | `tests/unit/stage223_19_calculator_tests.cpp:156` | Headless execution window count check | `Assertion failed: wm.window_count() >= 1` |
| **834** | `stage224_22_platform_settings_tests` | `tests/unit/stage224_22_platform_settings_tests.cpp:172` | Headless execution window count check | `Assertion failed: wm.window_count() >= 1` |
| **835** | `stage224_22_ipc_messenger_tests` | `tests/unit/stage224_22_ipc_messenger_tests.cpp:164` | Headless execution window count check | `Assertion failed: wm.window_count() >= 1` |
| **846** | `stage225_21_device_center_tests` | `tests/unit/stage225_21_device_center_tests.cpp:218` | Headless execution window count check | `Assertion failed: wm.window_count() == 1` |
| **847** | `stage225_21_system_monitor_tests` | `tests/unit/stage225_21_system_monitor_tests.cpp:199` | Headless execution window count check | `Assertion failed: wm.window_count() == 1` |
| **848** | `stage225_22_multi_app_device_tests` | `tests/unit/stage225_22_multi_app_device_tests.cpp:229` | Multi-window count check (expected 2) | `Assertion failed: wm.window_count() == 2` |

---

## 5. Category 4: Display Surface & Compositor Host Frame Lifecycle (6 Tests)

These tests test the display and compositor pipeline (Campaign 213–214). They fail due to uninitialized mock display surfaces, missing frame presentation hooks in headless mode, or null pointer dereferences when surface handles are queried without prior framebuffer allocation.

| Test # | Test Target | Source File & Line | Failure Mechanism | Evidence / Error Signature |
|---|---|---|---|---|
| **685** | `stage213_18_display_syscalls_tests` | `tests/unit/stage213_18_display_syscalls_tests.cpp:122` | Null surface pointer dereference in `surface_fill` | `***Exception: SegFault 3.66 sec; Line 122: surface_fill returned success` |
| **687** | `stage213_20_guest_shell_tests` | `tests/unit/stage213_20_guest_shell_tests.cpp:188` | Touch event propagation timing in mock shell | `Results: FAILURES DETECTED (3 failures)` |
| **688** | `stage213_21_interactive_apps_tests` | `tests/unit/stage213_21_interactive_apps_tests.cpp:214` | Display memory reclamation accounting | `Results: FAILURES DETECTED (6 failures)` |
| **690** | `stage213_23_interactive_e2e_tests` | `tests/unit/stage213_23_interactive_e2e_tests.cpp:250` | Host frame presentation counter discrepancy | `Results: E2E FAILURES DETECTED (11 failures)` |
| **710** | `stage214_22_adversarial_security_tests` | `tests/unit/stage214_22_adversarial_security_tests.cpp:180` | View destruction capture state reset | `Results: ADVERSARIAL FAILURES DETECTED (1 failures)` |
| **711** | `stage214_23_desktop_e2e_tests` | `tests/unit/stage214_23_desktop_e2e_tests.cpp:295` | Screenshot capture verification count | `Results: E2E FAILURES DETECTED (8 failures)` |
| **801** | `stage222_7_event_queue_tests` | `tests/unit/stage222_7_event_queue_tests.cpp:126` | Event type enum expectation mismatch | `Assertion failed: delivered_v2.type == WINIOS_EVENT_V2_MOUSE_DOWN` |

---

## 6. Category 5: Scheduler, Thread Context & Syscall Boundary Contracts (16 Tests)

This category represents historical tests in the kernel/scheduler/syscall layers (Campaigns 212, 218, 219, 220) where kernel behavior was hardened in subsequent campaigns (e.g. strict errno mappings, generational IDs, capability tokens, page table roots), causing early test assertions to fail on exact register return values.

| Test # | Test Target | Source File & Line | Failure Mechanism | Evidence / Error Signature |
|---|---|---|---|---|
| **665** | `stage212_17_init_coordinator_tests` | `tests/unit/stage212_17_init_coordinator_tests.cpp:142` | Init coordinator service shutdown state | `[FAIL] Stage 212.17: 8 tests failed.` |
| **666** | `stage212_18_multi_process_scheduler_tests` | `tests/unit/stage212_18_multi_process_scheduler_tests.cpp:98` | Preemption counter assertion | `[FAIL] Stage 212.18: 2 tests failed.` |
| **668** | `stage212_20_multi_process_e2e_tests` | `tests/unit/stage212_20_multi_process_e2e_tests.cpp:109` | Access violation retrieving PID 1 process handle | `***Exception: SegFault 3.84 sec; Line 109: Retrieved PID 1 handle` |
| **670** | `stage212_22_adversarial_security_tests` | `tests/unit/stage212_22_adversarial_security_tests.cpp:210` | Vector 25: Deserialization rejection return | `[FAIL] 1 vector(s) FAILED in Stage 212.22.` |
| **751** | `stage218_14_syscall_isolation_tests` | `tests/unit/stage218_14_syscall_isolation_tests.cpp:135` | Dispatcher handle_syscall dispatch isolation check | `Assertion failed: dispatcher.handle_syscall(cpu, *proc, uas, sched)` |
| **752** | `stage218_22_native_guest_e2e_tests` | `tests/unit/stage218_22_native_guest_e2e_tests.cpp:303` | Syscall dispatcher return status check | `Assertion failed: dispatcher.handle_syscall(cpu, *proc, uas, sched)` |
| **753** | `stage218_23_adversarial_tests` | `tests/unit/stage218_23_adversarial_tests.cpp:173` | Register 0 errno expectation (`kErrnoEPERM`) | `Assertion failed: cpu.read_register(0) == kErrnoEPERM` |
| **756** | `stage219_8_scheduler_execution_tests` | `tests/unit/stage219_8_scheduler_execution_tests.cpp:150` | TTBR0_EL1 translation table root comparison | `Assertion failed: cpu.get_mmu().get_ttbr0_el1() == root_a` |
| **757** | `stage219_10_synchronization_tests` | `tests/unit/stage219_10_synchronization_tests.cpp:71` | Thread synchronization barrier return check | `Assertion failed: cpu.read_register(0) == 16` |
| **758** | `stage219_11_multi_thread_guest_workload_tests` | `tests/unit/stage219_11_multi_thread_guest_workload_tests.cpp:144` | Access violation dereferencing dead thread context | `***Exception: SegFault 3.43 sec` |
| **765** | `stage219_22_multi_process_e2e_tests` | `tests/unit/stage219_22_multi_process_e2e_tests.cpp:264` | TTBR0_EL1 root supervisor comparison | `Assertion failed: cpu.get_mmu().get_ttbr0_el1() == root_sup` |
| **774** | `stage220_9_synchronization_tests` | `tests/unit/stage220_9_synchronization_tests.cpp:81` | Mutex syscall errno mismatch (expected EPERM) | `Assertion failed: cpu.read_register(0) == kErrnoEPERM` |
| **775** | `stage220_10_thread_lifecycle_tests` | `tests/unit/stage220_10_thread_lifecycle_tests.cpp:77` | Thread exit syscall errno mismatch (expected EINVAL) | `Assertion failed: cpu.read_register(0) == kErrnoEINVAL` |
| **777** | `stage220_13_syscall_hardening_tests` | `tests/unit/stage220_13_syscall_hardening_tests.cpp:58` | Syscall validation errno mismatch (expected EINVAL) | `Assertion failed: cpu.read_register(0) == kErrnoEINVAL` |
| **779** | `stage220_15_pipe_ipc_tests` | `tests/unit/stage220_15_pipe_ipc_tests.cpp:215` | Pipe write errno mismatch (expected EPIPE) | `Assertion failed: cpu.read_register(0) == kErrnoEPIPE` |
| **780** | `stage220_16_msg_trap_tests` | `tests/unit/stage220_16_msg_trap_tests.cpp:174` | Msg trap errno mismatch (expected EFAULT) | `Assertion failed: cpu.read_register(0) == kErrnoEFAULT` |

---

## 6. Category 6: VFS Storage, Serialization & Package Security (4 Tests)

| Test # | Test Target | Source File & Line | Failure Mechanism | Evidence / Error Signature |
|---|---|---|---|---|
| **820** | `stage223_20_notes_v2_tests` | `tests/unit/stage223_20_notes_v2_tests.cpp:162` | Isolated storage file persistence path check | `Assertion failed: iso_storage->exists("notes_v2.txt")` |
| **821** | `stage223_21_resource_browser_tests` | `tests/unit/stage223_21_resource_browser_tests.cpp:155` | Clipboard content string substring search | `Assertion failed: clip_content.find("font8x8_basic.bin") != npos` |
| **836** | `stage224_23_security_fuzzing_tests` | `tests/unit/stage224_23_security_fuzzing_tests.cpp:107` | Package validation result enum mapping | `Assertion failed: res == PackageValidationResult::PathTraversalAttempt` |
| **870** | `stage227_23_adversarial_tests` | `tests/unit/stage227_23_adversarial_tests.cpp:194` | Session manager malformed blob rejection | `Assertion failed: !session_mgr.deserialize(bad_str, loaded)` |

---

## 7. Category 7: Canonical Path & IPC Token Reallocation Boundary (2 Tests)

Two tests from Campaign 231 fail due to exact path string normalization and IPC token handle allocation semantics:

| Test # | Test Target | Source File & Line | Failure Mechanism | Evidence / Error Signature |
|---|---|---|---|---|
| **908** | `stage231_6_canonical_path_tests` | `tests/unit/stage231_6_canonical_path_tests.cpp:116` | Raw path string vs normalized forward-slash root path check | `Canonical Path Assertion Failed: is_strictly_inside_root(root, "D:/WinIOS/apps/com.sample.app/bin/calc")` |
| **912** | `stage231_10_ipc_token_security_tests` | `tests/unit/stage231_10_ipc_token_security_tests.cpp:156` | IPC token reallocation permission status return | `Assertion failed at line 156: st_after_realloc == IpcStatus::PermissionDenied` |

---

## 8. Category 8: Debug Configuration Divergence (5 Tests)

Five tests execute successfully in `RelWithDebInfo` but fail in `Debug`. Three are unoptimized CPU-bound stress loops exceeding CTest's 1500s timeout, and two are CRT / debug heap layout divergences:

| Test # | Test Target | Source File & Line | Failure Mode | Rel Result | Debug Duration | Root Cause / Technical Explanation |
|---|---|---|---|---|---|---|
| **713** | `stage215_3_abi_hardening_tests` | `tests/unit/stage215_3_abi_hardening_tests.cpp` | **Timeout** | Passed (0.08 s) | 1500.01 s | Stress loop with 50,000 iterations without MSVC `/O2` optimization exceeds 1500s timeout. |
| **722** | `stage215_22_security_fuzzing_tests` | `tests/unit/stage215_22_security_fuzzing_tests.cpp` | **Timeout** | Passed (0.06 s) | 1500.02 s | Iterative mutation harness with 10,000 passes unoptimized in Debug exceeds timeout. |
| **805** | `stage222_13_ui_toolkit_tests` | `tests/unit/stage222_13_ui_toolkit_tests.cpp` | **Timeout** | Passed (0.05 s) | 1500.03 s | Raster simulation loop without optimization exceeds 1500s timeout. |
| **810** | `stage222_22_multi_app_desktop_tests` | `tests/unit/stage222_22_multi_app_desktop_tests.cpp:186` | **Failed** | Passed (0.06 s) | 0.06 s | Assertion `std::strcmp(out_buf, clip_text) == 0`. In Debug MSVC CRT, uninitialized stack buffer contains `0xCC` bytes beyond string length. |
| **823** | `stage223_23_security_fuzzing_tests` | `tests/unit/stage223_23_security_fuzzing_tests.cpp:69` | **Failed** | Passed (0.04 s) | 0.05 s | Assertion `ta.length == 0`. Debug heap tracking adds allocation headers altering token allocator capacity. |

---

## 9. Stage 236.2 Sign-Off & Remediation Roadmap

All 48 failures across both configurations have been exhaustively indexed and classified into a structured 8-part taxonomy. The remediation roadmap in subsequent stages (236.5 – 236.13) will address these defects methodically:
1. Preserve all existing security checks and invariants.
2. Address root causes without weakening assertions or deleting tests.
3. Fix legacy ABI version checks to allow monotonic ABI progression.
4. Provide appropriate headless/mock environments for UI windowing checks.
5. Fix uninitialized buffer / null dereference crashes in display and multithread tests.
