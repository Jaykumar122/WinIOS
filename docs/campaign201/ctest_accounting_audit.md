# WinIOS Campaign 201 — Stage 201.1 CTest Accounting Audit

**Campaign:** 201  
**Stage:** 201.1  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target:** Independent reconciliation of CTest test counting and execution logs  
**Date:** November 16, 2026  

---

## 1. Executive Summary & Root Cause Analysis

Campaign 200 reported ambiguous test execution totals:
> *"470 total test targets. 470 passed. 0 failures. 1 skipped."*

This phrasing created a mathematical contradiction: if 470 tests were registered and 1 was skipped, then 469 passed ($469 + 1 = 470$), or if 470 passed and 1 was skipped, then 471 tests were registered ($470 + 1 = 471$).

An independent automated forensic audit of `build/CTestTestfile.cmake`, `docs/campaign200/test_results_release.txt`, and `docs/campaign200/test_results_debug.txt` was conducted via `scratch/audit_ctest_accounting.ps1`.

### Ground Truth Finding
1. **Registered Test Count:** Exactly **470** test targets are registered in CMake (`Test #1: memory_tests` to `Test #470: stage200_23_guest_workload_tests`).
2. **Executed & Evaluated Count:** Exactly **470** test targets were evaluated by CTest in both Debug and Release configurations.
3. **Passed Test Count:** Exactly **469** tests passed.
4. **Skipped Test Count:** Exactly **1** test was skipped (`Test #383: inferno_integration_tests`).
5. **Failed Test Count:** Exactly **0** tests failed.
6. **CTest Summary String:** CTest printed:
   ```text
   100% tests passed out of 470

   The following tests did not run:
   	383 - inferno_integration_tests (Skipped)
   ```
7. **Root Cause:** CTest's reporting logic computes the pass percentage relative to *executed, non-skipped* tests:
   $$\text{Effective Pass Percentage} = \frac{\text{Passed}}{\text{Total Registered} - \text{Skipped}} = \frac{469}{470 - 1} = \frac{469}{469} = 100.00\%$$
   The summary string `"100% tests passed out of 470"` was transcribed colloquially in the Campaign 200 report as "470 passed", while simultaneously noting that 1 test was skipped.

---

## 2. Independent Audit Ledger

The table below details the audit metrics computed independently by `scratch/audit_ctest_accounting.ps1` for both Release and Debug configurations:

| Metric | Release Log (`test_results_release.txt`) | Debug Log (`test_results_debug.txt`) | Discrepancy Status |
| :--- | :--- | :--- | :--- |
| **Total Registered Tests** | 470 | 470 | Exact match |
| **Parsed Test Results** | 470 | 470 | Exact match |
| **Passed Tests** | 469 | 469 | Exact match |
| **Skipped Tests** | 1 (`#383 inferno_integration_tests`) | 1 (`#383 inferno_integration_tests`) | Exact match |
| **Failed Tests** | 0 | 0 | Exact match |
| **CTest Summary Header** | `100% tests passed out of 470` | `100% tests passed out of 470` | Exact match |
| **Effective Pass Rate** | **100.0%** ($469 / 469$) | **100.0%** ($469 / 469$) | Exact match |
| **Raw Pass Rate** | **99.787%** ($469 / 470$) | **99.787%** ($469 / 470$) | Exact match |
| **Total Real Execution Time** | 62.75 seconds | 209.50 seconds | Expected (Debug ~3.3x slower) |

---

## 3. Analysis of Skipped Test #383 (`inferno_integration_tests`)

Inspection of `CMakeLists.txt` lines 2570–2585 explains why Test #383 is skipped:
```cmake
add_test(NAME inferno_integration_tests COMMAND inferno_integration_tests)
set_tests_properties(inferno_integration_tests PROPERTIES SKIP_RETURN_CODE 127)
```
When `inferno_integration_tests.exe` runs without external proprietary research disk images or assets, it returns status code `127`, signaling CTest that the test environment prerequisites were not present, thereby triggering an architectural skip rather than a failure.

---

## 4. Formal Standardization for Campaign 201

To prevent reporting ambiguity throughout Campaign 201 and all future milestones, the following standardized terminology and schema must be strictly enforced:
- **Registered Tests ($N_{reg}$):** The total number of targets registered with `add_test()` in CMake.
- **Passed Tests ($N_{pass}$):** The count of tests that returned exit code 0 and satisfied all assertions.
- **Skipped Tests ($N_{skip}$):** The count of tests intentionally skipped via `SKIP_RETURN_CODE`.
- **Failed Tests ($N_{fail}$):** The count of tests terminating with non-zero exit code or uncaught assertions.
- **Total Balance Invariant:**
  $$N_{reg} = N_{pass} + N_{skip} + N_{fail}$$
  For baseline commit `c569f42`: $470 = 469 + 1 + 0$.

All Campaign 201 reports and manifests will use this exact formula.
