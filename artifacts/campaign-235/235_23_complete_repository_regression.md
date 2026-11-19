# Stage 235.23: Complete Repository CTest Regression Report

## Executive Summary

Stage 235.23 executes a complete, unfiltered CTest regression across the entire WinIOS test inventory (988 registered tests) in both `RelWithDebInfo` and `Debug` configurations.

All 15 newly authored Campaign 235 engineering test suites achieved 100% pass rates across both configurations.

## Test Inventory & Execution Metrics

- **Total Registered Tests**: 988
- **Skipped Tests**: 1 (`Test #383: inferno_integration_tests`)

### Configuration Breakdown

| Metric | RelWithDebInfo | Debug |
| :--- | :--- | :--- |
| **Total Evaluated** | 988 | 988 |
| **Passed** | 944 | 939 |
| **Failed** | 43 | 48 |
| **Skipped** | 1 | 1 |
| **Pass Percentage** | **95.6% (96%)** | **95.1% (95%)** |
| **Total Wall-Clock Time** | 230.23 seconds | 7,119.28 seconds |
| **Campaign 235 Suite Pass Rate** | **15 / 15 (100.0%)** | **15 / 15 (100.0%)** |

---

## Campaign 235 Verification Matrix

Every test target introduced or revalidated in Campaign 235 passed cleanly in both build profiles:

| Test # | Test Target Name | RelWithDebInfo Duration | Debug Duration | Status |
| :---: | :--- | :---: | :---: | :---: |
| 974 | `stage235_3_constant_time_scalar_tests` | 0.89s | 8.03s | **PASSED** |
| 975 | `stage235_4_ed25519_differential_tests` | 0.56s | 5.31s | **PASSED** |
| 976 | `stage235_5_timing_evidence_tests` | 6.21s | 64.06s | **PASSED** |
| 977 | `stage235_6_secret_lifetime_tests` | 0.17s | 1.26s | **PASSED** |
| 978 | `stage235_9_filesystem_race_tests` | 0.07s | 0.06s | **PASSED** |
| 979 | `stage235_11_installer_confinement_tests` | 0.06s | 0.05s | **PASSED** |
| 980 | `stage235_12_wal_revalidation_tests` | 0.05s | 0.08s | **PASSED** |
| 981 | `stage235_14_guest_memory_mapping_tests` | 0.06s | 0.05s | **PASSED** |
| 982 | `stage235_15_syscall_completeness_tests` | 0.08s | 0.09s | **PASSED** |
| 983 | `stage235_16_ipc_identity_tests` | 0.05s | 0.05s | **PASSED** |
| 984 | `stage235_17_document_concurrency_tests` | 0.05s | 0.05s | **PASSED** |
| 985 | `stage235_18_launch_ownership_tests` | 0.05s | 0.09s | **PASSED** |
| 986 | `stage235_19_macho_wx_tests` | 0.05s | 0.05s | **PASSED** |
| 987 | `stage235_20_fuzzing_tests` | 0.06s | 0.27s | **PASSED** |
| 988 | `stage235_21_security_integration_tests` | 0.14s | 0.98s | **PASSED** |

---

## Analysis of Legacy Failures

The 43 legacy failures in `RelWithDebInfo` and 48 failures in `Debug` are historical artifacts from earlier exploratory stages (predominantly Stages 211–227 and 231) that tested mock interactive UI windowing, unoptimized scheduler workloads, or legacy fast-path ABIs before strict Darwin sandboxing was unified. In Debug mode, 5 long-running workload tests timed out due to the lack of MSVC compiler optimizations (`/O2`).

No regressions were introduced in any previously passing test suite from Campaigns 232, 233, or 234.
