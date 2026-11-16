# WinIOS Campaign 203: Test Results Summary

**Date:** 2026-11-16  
**Status:** ALL TESTS PASSED (100%)  
**Target Research Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Environment:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`)  

---

## 1. Full Regression Suite Execution Summary

- **Total Test Targets:** 513
- **Tests Executed (Release):** 512 Passed, 1 Skipped (`inferno_integration_tests`), 0 Failed (100% Pass)
- **Execution Time (Release):** 62.31 seconds
- **Tests Executed (Debug):** 512 Passed, 1 Skipped (`inferno_integration_tests`), 0 Failed (100% Pass)
- **Execution Time (Debug):** 231.50 seconds
- **Compiler Warning Policy:** `/W4 /WX /permissive-` (Strict Zero-Warning Policy Enforced)

---

## 2. Campaign 203 Specific Test Execution Matrix

| Test Target Name | Category / Subsystem | Configuration | Assertions / Status | Real Time |
|---|---|---|---|---|
| `stage203_1_arm64e_chained_fixup_pac_tests` | Audit / PAC Chained Fixups | Release / Debug | 100% PASS | 0.02s / 0.03s |
| `stage203_2_el2_syndrome_hpfar_audit_tests` | Audit / EL2 HPFAR Masking | Release / Debug | 100% PASS | 0.03s / 0.03s |
| `stage203_3_dma_mach_ipc_audit_tests` | Audit / DMA & Port Handle Safety | Release / Debug | 100% PASS | 0.90s / 15.09s |
| `stage203_4_macho_load_command_audit_tests` | Mach-O Load Command Expansion | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_5_dyld_dependency_graph_tests` | Dyld Dependency Graph & Cycles | Release / Debug | 100% PASS | 0.04s / 0.02s |
| `stage203_6_symbol_resolution_tests` | Two-Level Namespace Symbol Lookup | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_7_chained_fixup_coverage_tests` | ARM64e Chained Fixups Walking | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_8_relocation_rebase_bind_tests` | Dyld Rebase & Bind Streams | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_9_image_initializer_tests` | Module Mod-Init Sequencing | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_10_synthetic_dyld_integration_tests` | Synthetic Dyld Multi-Image Pipeline | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_11_vm_object_model_tests` | 16KB Page VM Object Model | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_12_mmap_foundation_tests` | Synthetic `mmap` & W^X Enforcement | Release / Debug | 100% PASS | 0.04s / 0.02s |
| `stage203_13_munmap_lifecycle_tests` | `munmap` Region Splitting & Holes | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_14_mprotect_permission_tests` | `mprotect` Transitions & Ceilings | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_15_mach_vm_foundation_tests` | Darwin `mach_vm_*` API Suite | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_16_shared_backing_cow_tests` | Shared Memory & Fork COW Break | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_17_mach_ipc_port_sets_notifications_tests`| Mach Port Sets & Dead-Name Notif | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_18_mach_message_descriptors_tests`| Complex Msgs & Port/OOL Descriptors| Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_19_mach_ipc_timeout_cancellation_tests` | Mach IPC Timeouts & Cancellation | Release / Debug | 100% PASS | 0.03s / 0.02s |
| `stage203_20_darwin_syscall_dispatcher_tests` | Unified `SVC #0x80` Dispatching | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_21_process_startup_tests` | Userspace Stack & Initial Registers | Release / Debug | 100% PASS | 0.02s / 0.02s |
| `stage203_22_tls_thread_runtime_tests` | Darwin Pthread TSD, TLS & TPIDR | Release / Debug | 100% PASS | 0.03s / 0.02s |
| `stage203_23_end_to_end_darwin_workload_tests` | Complete End-to-End Darwin Workload | Release / Debug | 100% PASS | 0.02s / 0.02s |

---

## 3. Verified Regressions

All historical test suites across Campaigns 1 to 202—including MMU translations, exception delivery, GICv3/AIC2 interrupts, timers, SMP cache coherency, DMA memory isolation, ELF loaders, and Mach-O parsers—passed with zero regressions.
