# WinIOS Campaign 224 — Test Results Summary

**Campaign:** 224  
**Date:** 2026-11-18  
**Total New Test Suites:** 13  
**Pass Rate:** 100% (13 / 13 suites passed)  
**Total Repository Tests:** 836  
**Zero Regressions:** Verified across all Stage 221, 222, 223, and 224 suites.

---

## 1. Stage 224 Test Execution Matrix

| Test Suite Target | Stage | Verification Focus | Result | Time |
| :--- | :--- | :--- | :--- | :--- |
| `stage224_3_package_validation_tests` | 224.3, 224.4 | WIOSPKG2 format, header bounds, SHA-256 integrity, manifest parsing | **PASSED** | 0.02s |
| `stage224_5_app_registry_tests` | 224.5 | Application Registry CRUD, active state filtering, serialization | **PASSED** | 0.02s |
| `stage224_6_package_installer_tests` | 224.6, 224.7, 224.8 | Staging install, transactional upgrade, automatic rollback, uninstall | **PASSED** | 0.02s |
| `stage224_9_permissions_identity_tests` | 224.9, 224.10 | Process AppIdentity tokens, capability masks, audit telemetry | **PASSED** | 0.02s |
| `stage224_11_storage_quota_recovery_tests`| 224.11, 224.12 | Recursive storage quota, in-place overwrite netting, `.tmp.*` purge | **PASSED** | 0.02s |
| `stage224_13_ipc_reliability_tests` | 224.13, 224.14 | 64-bit generation handles, ABA defense, named port lookup, queue bounds | **PASSED** | 0.02s |
| `stage224_15_supervisor_crash_tests` | 224.15, 224.16 | Process tracking, graceful termination, window cleanup, failure taxonomy | **PASSED** | 0.03s |
| `stage224_17_configuration_tests` | 224.17 | Typed KV configuration (string, int64, double, bool), VFS persistence | **PASSED** | 0.02s |
| `stage224_18_diagnostics_logging_tests` | 224.18 | Structured ring buffer logging, severity filtering, platform summary | **PASSED** | 0.02s |
| `stage224_19_desktop_launcher_tests` | 224.19, 224.20 | Dynamic registry discovery, item selection, single-instance focus | **PASSED** | 0.02s |
| `stage224_22_platform_settings_tests` | 224.22 | Genuine ARM64 EL0 Platform Settings workload execution (Exit 81) | **PASSED** | 0.15s |
| `stage224_22_ipc_messenger_tests` | 224.22 | Genuine ARM64 EL0 IPC Messenger workload execution (Exit 82) | **PASSED** | 0.14s |
| `stage224_23_security_fuzzing_tests` | 224.23 | Malformed packages, path traversal, quota exhaustion, stale handles | **PASSED** | 0.03s |

**Overall Stage 224 Execution Time:** 0.59s  
**Overall Regression Run (Campaigns 223 + 224, 25 tests):** 1.67s (100% Passed)

---

## 2. Workload Instruction Retirement & Conformance

Both new native ARM64 guest workloads were compiled using Zig 0.17.0 (`-target aarch64-macos`), linked with `guest-sdk/lib/crt0.s`, and executed in the CPU emulator:

1. **Platform Settings (`com.winios.settings`):**
   - Binary Size: 52,960 bytes Mach-O 64-bit executable.
   - Entry Point: `0x1000003D0`.
   - Syscalls Exercised: `winios_config_set`, `winios_config_get`, `winios_diag_get_summary`, `SYS_exit` (1).
   - Exit Code: **81** (Clean exit upon successful configuration verification).
2. **IPC Messenger (`com.winios.messenger`):**
   - Binary Size: 52,704 bytes Mach-O 64-bit executable.
   - Entry Point: `0x1000003D0`.
   - Syscalls Exercised: `winios_ipc_create_port_v2`, `winios_ipc_send_v2`, `winios_ipc_recv_v2`, `SYS_exit` (1).
   - Exit Code: **82** (Clean exit upon successful IPC echo loopback).
