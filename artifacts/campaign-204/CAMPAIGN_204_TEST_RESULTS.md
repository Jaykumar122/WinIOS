# WinIOS Campaign 204 — Test Results Report

**Campaign:** 204  
**Date:** November 16, 2026  
**Host Environment:** Windows 11 x64  
**Toolchain:** Visual Studio 2026 / MSVC 19.44, C++20 (`/W4 /WX /permissive-`)  
**Git Branch:** `campaign-191r`  
**Git Baseline Commit:** `94fe630f68b69c51e8642ae6fc6a3a39e53d6432`  

---

## 1. Test Suite Summary

In strict accordance with WinIOS testing policy:
- Skipped tests are **NEVER** counted as passed.
- All test runs require exact accounting: Registered, Passed, Skipped, and Failed.

### Release Configuration Results
- **Registered Executables in CTest:** 536
- **Passed Test Executables:** 535
- **Skipped Test Executables:** 1 (`383 - inferno_integration_tests`, skipped by environmental policy due to missing bare-metal Inferno board)
- **Failed Test Executables:** 0
- **Total Test Execution Time:** 69.50 seconds
- **Pass Rate:** 100% of runnable suites (535 / 535)

### Debug Configuration Results
- **Registered Executables in CTest:** 536
- **Passed Test Executables:** 535
- **Skipped Test Executables:** 1 (`383 - inferno_integration_tests`)
- **Failed Test Executables:** 0
- **Total Test Execution Time:** 327.35 seconds
- **Pass Rate:** 100% of runnable suites (535 / 535)

---

## 2. Campaign 204 Test Suites (Stages 204.1 – 204.23)

| Test Executable Name | Focus Area | Result | Runtime (Release) |
|---|---|---|---|
| `stage204_1_hpfar_el2_reverification_tests` | Stage-2 address shift identity `(HPFAR >> 4) << 12 == HPFAR << 8` across 1M samples | **PASSED** | 0.55s |
| `stage204_2_dma_benchmark_reproducibility_tests` | Rigorous 5-trial DMA benchmark, standard deviation and fast-path copy speedup | **PASSED** | 4.58s |
| `stage204_3_mach_ipc_reconciliation_tests` | Generation-safe port naming, ABA protection, and legacy port compatibility | **PASSED** | 0.03s |
| `stage204_4_macho_fixture_corpus_tests` | 12 synthetic Mach-O binary corpus generation and structural validation | **PASSED** | 0.03s |
| `stage204_5_segment_mapping_vm_tests` | 16KB page granule segment mapping, W^X enforcement, zero-fill allocation | **PASSED** | 0.02s |
| `stage204_6_export_trie_metadata_tests` | Export trie cycle detection, recursion depth limits (<= 64), stub resolvers | **PASSED** | 0.02s |
| `stage204_7_binding_resolution_tests` | Two-level namespace symbol binding, coalescing, weak imports, flat lookup | **PASSED** | 0.02s |
| `stage204_8_rpath_dependency_identity_tests` | `@rpath`, `@loader_path`, `@executable_path` search path expansion and resolution | **PASSED** | 0.02s |
| `stage204_9_loader_relocation_protection_tests` | Post-relocation read-only enforcement (`mprotect` `__DATA_CONST` to `R--`) | **PASSED** | 0.02s |
| `stage204_10_image_initialization_order_tests` | Topological dependency order for `__mod_init_func` and reverse `__mod_term_func` | **PASSED** | 0.02s |
| `stage204_11_libsystem_compatibility_tests` | Synthetic libSystem stubs, string functions, heap, errno, and stack canary | **PASSED** | 0.02s |
| `stage204_12_vfs_architecture_tests` | VFS vnode hierarchy, pseudo-devices (`/dev/null`, `/dev/zero`, `/dev/random`), symlinks | **PASSED** | 0.02s |
| `stage204_13_file_descriptors_io_tests` | Per-process file table, standard I/O (0, 1, 2), `SYS_read`, `SYS_write`, `SYS_dup2` | **PASSED** | 0.03s |
| `stage204_14_bsd_syscall_dispatcher_tests` | BSD syscall expansion (20+ calls), memory protection, process credentials, sysctl | **PASSED** | 0.03s |
| `stage204_15_process_identity_env_tests` | Process creation, Darwin stack frame initialization (`argc`, `argv`, `envp`, auxv) | **PASSED** | 0.02s |
| `stage204_16_thread_lifecycle_tests` | POSIX thread creation, thread stacks, entry points, state transitions, join | **PASSED** | 0.03s |
| `stage204_17_pthread_sync_tests` | Pthread mutexes (init/lock/trylock/unlock) and condition variables with futexes | **PASSED** | 0.04s |
| `stage204_18_instruction_coverage_tests` | ARM64 opcode matrix verification: ALU, branches, PC-relative, loads/stores, syscalls | **PASSED** | 0.02s |
| `stage204_19_application_runner_tests` | Integrated application runner: binary loading, linking, startup, and execution | **PASSED** | 0.06s |
| `stage204_20_execution_tracing_tests` | Cycle diagnostics, instruction disassembly, register diffs, memory logs, JSON export | **PASSED** | 0.02s |
| `stage204_21_workload_compatibility_tests` | Verification of all 12 corpus fixtures against execution and link criteria | **PASSED** | 0.14s |
| `stage204_22_security_hardening_tests` | Security stress test: path traversal, integer overflow, file exhaustion, trie depth | **PASSED** | 0.04s |
| `stage204_23_e2e_workload_demos_tests` | End-to-end multi-image execution, VFS file operations, pthread concurrency & TLS | **PASSED** | 0.06s |

---

## 3. Regression Suite Verification
All 512 historical test suites from Campaigns 191 through 203 were executed in both Release and Debug configurations. Zero regressions remain. All 13 historical sensitivity points identified during Stage 204.24 audit were successfully reconciled and permanently hardened against regressions.
