# Multi-Core Deterministic Replay Test Matrix

## 1. Test Suite Architecture

The test suite for Phase 52 Stage 26 is implemented in `tests/unit/smp_replay_tests.cpp`. It exercises all components of the multi-core deterministic replay system across 9 comprehensive groups (Groups A through I).

---

## 2. Comprehensive Test Matrix

| Group | Category | Test Count | Description | Status |
|:---:|:---|:---:|:---|:---:|
| **A** | State Machine & Gate | 4 | Evaluates `SmpReplayState` transitions (`Uninitialized` $\rightarrow$ `Prepared` $\rightarrow$ `Running` $\rightarrow$ `Completed`/`Diverged`), enforces 2-core limit, rejects 4/8 cores, and enforces `allow_experimental_smp`, `permit_host_dma`, `permit_host_wall_clock`. | PASS |
| **B** | Snapshot Restoration | 3 | Verifies `.wsnap v2` cluster restoration, canonical cluster hash verification, core state integrity, shared memory restoration, and detects corrupted snapshot hash. | PASS |
| **C** | Event Cursor & Ordering | 4 | Verifies strict monotonic sequence reading ($S_{i+1} = S_i + 1$), out-of-order sequence rejection, end-of-stream detection, and CRC32C chunk checksum verification. | PASS |
| **D** | Scheduler Replay | 4 | Validates replay of `CoreSelected`, `QuantumStart`, `QuantumEnd`, and `EarlyTruncation` events. Ensures scheduler decisions match recorded sequence. | PASS |
| **E** | Retirement Synchronization | 4 | Confirms exact global ($R_{global}$) and per-core ($R_{core}$) instruction retirement tracking. Asserts that faulting/aborted instructions do not increment retirement counters. | PASS |
| **F** | Event Injection | 3 | Verifies exact retirement-matched injection of `ExternalInterrupt` (SPI/PPI delivery) and `MmioRead` input data to target core. | PASS |
| **G** | Divergence Detection | 4 | Injects adversarial perturbations (wrong core selected, truncated quantum, corrupted MMIO payload, out-of-order retirement) and asserts immediate halt at first divergence. | PASS |
| **H** | Multi-Run Replay Invariance | 2 | Runs 5 consecutive full replays of recorded 2-core workload and validates 100% bit-identical canonical cluster hash across all runs. | PASS |
| **I** | Compatibility & Rejection | 3 | Validates single-core `.wsnap v1` / `.sndl v1` replay backward compatibility and verifies rejection of SMP replay when experimental flag is disabled. | PASS |

---

## 3. Test Execution Summary

- Total Test Groups: 9
- Total Test Cases: 31
- Pass Rate: 100% (31/31)
- Failures: 0
- Skipped: 0
- Test Target: `smp_replay_tests`
- Execution Time (Release): ~0.04s
- Execution Time (Debug): ~0.15s
