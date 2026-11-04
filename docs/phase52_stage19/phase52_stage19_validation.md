# WinIOS Phase 52, Stage 19 — Replay Validation Architecture and Test Plan

## 1. Overview

The validation architecture for Phase 52 Stage 19 guarantees deterministic correctness across normal execution, boundary edge cases, input anomalies, and corruption vectors.

Validation comprises 9 distinct test groups in `tests/unit/replay_tests.cpp` and an end-to-end performance suite in `tests/unit/replay_benchmark.cpp`.

---

## 2. Test Group Architecture

```
+-------------------------------------------------------------------------------+
|                       Replay Test Suite (replay_tests.cpp)                    |
+-------------------------------------------------------------------------------+
| Test 1: test_baseline_snapshot_validation_and_restore                         |
|         - Validates .wsnap headers, section directory, and payload CRCs       |
|         - Rejects invalid magic and truncated/corrupted snapshots             |
|                                                                               |
| Test 2: test_sndl_event_cursor                                                |
|         - Validates block-by-block sequential event streaming                 |
|         - Verifies sequence monotonicity and stream exhaustion handling       |
|                                                                               |
| Test 3: test_single_core_pure_compute                                         |
|         - End-to-end replay of pure AArch64 compute workload                  |
|         - Validates exact register and guest memory state match               |
|                                                                               |
| Test 4: test_nondeterministic_inputs_replay                                   |
|         - Replays non-deterministic inputs (counter reads, interrupts)        |
|         - Verifies interception without querying live host timers             |
|                                                                               |
| Test 5: test_state_hash_verification                                          |
|         - Validates component state hashing (CPU, SIMD, Sysregs, RAM, GIC)    |
|         - Confirms single-byte memory perturbation triggers diff diagnostics  |
|                                                                               |
| Test 6: test_checkpoints_tracking                                             |
|         - Verifies periodic checkpoints recorded at exact retirement intervals|
|         - Ensures state hashes are tracked monotonically                      |
|                                                                               |
| Test 7: test_divergence_detection                                             |
|         - Injects intentional sequence and core mismatch vectors              |
|         - Verifies immediate halt at first divergence and report generation   |
|                                                                               |
| Test 8: test_repeatability_10_runs                                            |
|         - Executes 10 consecutive full replays of the same workload           |
|         - Asserts bit-for-bit identical final state hashes across all runs    |
|                                                                               |
| Test 9: test_smp_unsupported_rejection                                        |
|         - Confirms multi-core mode is safely rejected with error code 9       |
+-------------------------------------------------------------------------------+
```

---

## 3. Strict Repeatability Acceptance Requirement

To satisfy the primary acceptance criterion of Stage 19:
- **Requirement**: At least 10 repeated single-core replay runs must produce bit-for-bit identical supported final state.
- **Verification Method**: Test 8 executes the complete replay pipeline across 10 iterations from a restored snapshot and SNDL log. Each iteration computes the 64-bit canonical combined state hash:
  $$\text{CombinedHash}_i = \text{CombinedHash}_1 \quad \forall i \in \{1, \dots, 10\}$$
- If any divergence or hash deviation occurs in any iteration, the test immediately aborts.
