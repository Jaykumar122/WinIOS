# Campaign 195 — Evidence Matrix & Mandatory Acceptance Verification

## 1. Mandatory Acceptance Tests (A through I)

| Test ID | Test Category | Implementation & Verification | Result | Status |
|:---:|:---|:---|:---:|:---:|
| **Test A** | Full GPR Preservation (X0–X30) across Live Preemption | `tests/unit/stage195_4_asynchronous_preemption_tests.cpp`<br>17,396 instructions retired, 174 preemptions handled. All GPRs verified intact | Zero Corruption | **PASSED** |
| **Test B** | SIMD/FP Context Preservation (Q0–Q31, FPCR, FPSR) | `tests/unit/stage195_4_asynchronous_preemption_tests.cpp`<br>32 128-bit vector registers + FPCR/FPSR verified isolated across tasks | 100% Vector Invariant | **PASSED** |
| **Test C** | PSTATE Preservation (NZCV, DAIF, SPSR, ELR, ERET) | `tests/unit/stage195_4_asynchronous_preemption_tests.cpp`<br>Condition flags and interrupt masks restored atomically without flag leakage | Atomic Restoration | **PASSED** |
| **Test D** | Arbitrary Instruction-Boundary Preemption | `tests/unit/stage195_4_asynchronous_preemption_tests.cpp`<br>Timer tick interrupts guest computation at arbitrary instruction boundaries | Safe Trap & Resume | **PASSED** |
| **Test E** | Multi-Task Preemptive Switching with Independent Stacks & TLS | `tests/unit/stage195_4_asynchronous_preemption_tests.cpp`<br>3 threads scheduled with distinct stack bases and TPIDR_EL0/EL1 | Zero Cross-Talk | **PASSED** |
| **Test F** | Two-Core SMP Task Migration | `tests/unit/stage195_10_timer_and_sync_stress_tests.cpp`<br>Task migrated from Core 0 to Core 1; dual-running prevented | Zero Dual-Running | **PASSED** |
| **Test G** | Wait-Queue Races & Lost-Wakeup Prevention | `tests/unit/stage195_10_timer_and_sync_stress_tests.cpp`<br>Priority wake order, FIFO wake order, wake immediately before block | Zero Lost Wakeups | **PASSED** |
| **Test H** | Concurrent Memory Faults & Demand-Paging Race Prevention | `tests/unit/stage195_15_demand_paging_concurrency_tests.cpp`<br>Dual-core disjoint faults, atomic same-page resolution, TLB invalidation | Zero Double-Alloc | **PASSED** |
| **Test I** | Deterministic Replay of Long-Running Synthetic Workloads | `tests/unit/stage195_17_long_running_workload_tests.cpp`<br>Run 1 and Run 2 produced identical retired counts and state hashes | Bit-for-bit Replay | **PASSED** |

---

## 2. Regression Test Results Summary

| Build Configuration | Total Tests | Passed | Skipped | Failed | Pass Rate |
|:---|:---:|:---:|:---:|:---:|:---:|
| **Release** | 390 | 389 | 1 (`inferno_integration_tests`) | 0 | **100%** |
| **Debug** | 390 | 389 | 1 (`inferno_integration_tests`) | 0 | **100%** |

Log files:
- Release: `docs/campaign195/test_results_release.txt`
- Debug: `docs/campaign195/test_results_debug.txt`
