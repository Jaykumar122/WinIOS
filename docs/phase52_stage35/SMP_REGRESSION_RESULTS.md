# Multi-Core SMP Regression Results (1, 2, 4, 8 Cores)
**Stage 26–34 Determinism & Non-Regressing Parity Verification**

---

## 1. Regression Test Execution Summary

All previously certified SMP replay test suites from Stages 26–34 were executed under both Release and Debug configurations to verify zero regressions.

| Test Binary | Target Topology | Execution Scope | Release Result | Debug Result |
|---|---|---|---|---|
| `smp_foundation_tests` | 1, 2, 4 Cores | Core scheduling, retirement | Passed (0.05s) | Passed (0.08s) |
| `smp_four_core_replay_tests` | 4 Cores | Deterministic 4-core replay | Passed (0.30s) | Passed (0.41s) |
| `smp_eight_core_architecture_tests` | 8 Cores | Architecture, topology validation | Passed (0.04s) | Passed (0.05s) |
| `smp_eight_core_replay_tests` | 8 Cores | Deterministic 8-core replay | Passed (0.35s) | Passed (0.52s) |
| `smp_eight_core_replay_hardening_tests` | 8 Cores | Stress, interrupt contention | Passed (0.21s) | Passed (0.26s) |
| `smp_long_duration_replay_tests` | 1, 2, 4, 8 Cores | 100,000 cycles, cross-topology | Passed (6.73s) | Passed (93.18s)|
| `smp_cross_topology_tests` | 1, 2, 4, 8 Cores | Topology transition invariance | Passed (0.16s) | Passed (0.18s) |
| `smp_endurance_certification_tests`| 8 Cores | Extended endurance certification| Passed (1.71s) | Passed (9.97s) |

---

## 2. 10-Run Deterministic Repeatability

Across 10 consecutive full 8-core replay runs:
- Canonical Cluster Hash: `0x959c27b5d4286ffa` (Identical in 10 / 10 runs).
- Extended Cluster Hash: Constant and reproducible across all 10 runs.
- Bit-for-bit replay divergence count: **0**.
