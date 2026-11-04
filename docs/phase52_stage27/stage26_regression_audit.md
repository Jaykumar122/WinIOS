# Stage 26 Regression & Architecture Audit

## 1. Audit Objectives

Before introducing 4-core SMP replay, an architectural audit of the Stage 26 implementation was conducted to ensure:
1. Two-core deterministic replay behavior is preserved with zero regression.
2. Single-core `.wsnap v1` and `.sndl v1` replay paths remain untouched and functional.
3. No fixed 2-core assumptions in data structures impede clean generalization to 4 cores.

---

## 2. Findings on Core Topology Assumptions

| Subsystem | Stage 26 Design | Stage 27 Generalization |
|---|---|---|
| `SmpReplayCoordinator::config_` | Hardcoded `max_supported_cores{2}` | Dynamically accepts 2 or 4 cores based on capability config |
| `SmpReplayCoordinator::active_core_count_` | Implicitly 2 | Explicit member variable matching snapshot core count |
| `SmpReplayCoordinator::core_replay_states_` | Resized to 2 | Resized to `active_core_count_` (2 or 4) |
| `SmpReplayCoordinator::stats_.per_core_retired` | Resized to 2 | Resized to `active_core_count_` (2 or 4) |
| `ReplaySession::prepare` | Rejected `cc > 2` | Permits `cc == 4` when `allow_four_core_replay` is set |
| `SmpScheduler` | Dynamically supports $N$ cores (default 16) | Fully topology-independent; instantiated with `active_core_count_` |
| `SmpSharedMemory` | Dynamically supports $N$ cores | Instantiated with `active_core_count_` |
| `SmpExclusiveMonitorManager` | Dynamically supports $N$ cores | Instantiated with `active_core_count_` |
| `SmpInterruptRouter` | Dynamically routes to any core ID | Fully topology-independent |
| `SmpSnapshotRestorer` | Restores arbitrary core counts up to cluster size | Successfully restores 4 cores |

---

## 3. Regression Verification Results

- **Two-Core Replay Suite (`smp_replay_tests`)**:
  - All 9 groups (Groups A through I) passed without modification.
- **Single-Core Replay Suite (`replay_tests`, `replay_hardening_tests`)**:
  - 100% pass rate.
- **Full CTest Suite**:
  - 63/63 targets passed (62 passed, 1 skipped).
