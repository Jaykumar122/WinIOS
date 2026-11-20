# Stage 241.17: Four-Workload End-to-End Execution Matrix

## 1. Unified Execution Suite
Implemented `tests/unit/stage241_17_four_workload_e2e_tests.cpp` to execute all four compiled workloads in a single unified test matrix:

| Workload Name | Binary Format | PID | Instructions | Exit Code | Expected Exit | Status |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **`arith_control`** | Freestanding Mach-O | 201 | 14,249 | **42** | 42 | **PASS** |
| **`mem_func`** | Freestanding Mach-O | 202 | 1,821 | **55** | 55 | **PASS** |
| **`syscall_lifecycle`** | Freestanding Mach-O | 203 | 128 | **77** | 77 | **PASS** |
| **`multi_app_worker`** | Freestanding Mach-O | 204 | 1,180 | **88** | 88 | **PASS** |

## 2. Exit Code Reconciliation
Campaign 240 stage 240.15 documented the exit codes for the first three workloads (42, 55, 77), while `multi_app_worker` was run under stage 240.19. Stage 241.17 explicitly reconciles and documents all 4 workloads under a single test harness.
