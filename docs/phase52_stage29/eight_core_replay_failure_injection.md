# Eight-Core Replay Adversarial Failure Injection Results

## 1. Overview
Hardening suite `smp_eight_core_replay_hardening_tests` executes 14 adversarial failure injection scenarios to verify robust failure handling without crashes or undefined behavior.

## 2. Failure Injection Test Results

| Test Group | Fault Injected | Expected Replay Error Code | Result | Diagnostic Verification |
|---|---|---|---|---|
| Group 1 | Truncated/corrupted `.wsnap v2` file | `SnapshotValidationFailed` | **PASS** | State set to `Diverged` with file size error |
| Group 2 | Mismatched snapshot vs SNDL core count (8 vs 4) | `UnsupportedConfiguration` | **PASS** | Gating rejected core mismatch |
| Group 3 | Out-of-bounds core ID in record (`core_id = 15`) | Divergence: `CoreMismatch` | **PASS** | Caught before execution |
| Group 4 | Non-monotonic global sequence ($S_{10} \to S_{12}$) | Divergence: `SequenceMismatch` | **PASS** | Monotonicity violation flagged |
| Group 5 | Premature stream exhaustion | Divergence: `MissingEvent` | **PASS** | Stream ended before quantum complete |
| Group 6 | Mutated instruction retirement count | Divergence: `RetirementMismatch` | **PASS** | Exact slice retirement mismatch caught |
| Group 7 | Incomplete recording (missing footer) | `SndlValidationFailed` | **PASS** | Missing footer rejected |
| Group 8 | Dropped events in footer (`dropped_events > 0`) | `SndlValidationFailed` | **PASS** | Buffer overflow rejected |
| Group 9 | Baseline snapshot hash mismatch in SNDL header | `SnapshotMismatch` | **PASS** | Pre-flight hash check failed |
| Group 10 | First-divergence diagnostic payload check | Divergence flagged | **PASS** | Full diagnostic payload populated |
| Group 11 | Host DMA capability violation (`allow_uncaptured_dma = true`) | `UnsupportedConfiguration` | **PASS** | BLK-SMP-03 protection held |
| Group 12 | Host clock capability violation (`clock_source = HostClock`) | `UnsupportedConfiguration` | **PASS** | BLK-SMP-04 protection held |
| Group 13 | Complete capability gate truth table (16 combinations) | `UnsupportedConfiguration` / `Success` | **PASS** | Strict dual opt-in enforced |
| Group 14 | Multi-topology format compatibility (1, 2, 4, 8 cores) | Validated across topologies | **PASS** | All valid topologies accepted |
