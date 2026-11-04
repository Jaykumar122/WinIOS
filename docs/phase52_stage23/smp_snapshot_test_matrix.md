# Multi-Core Snapshot Test Matrix (`smp_snapshot_tests`)

## 1. Test Suite Summary

The `smp_snapshot_tests` executable contains 22 automated tests across 10 distinct functional groups.

Build configurations tested:
- **Debug Configuration**: 22 passed, 0 failed.
- **Release Configuration**: 22 passed, 0 failed.

---

## 2. Test Execution Matrix

| Group | Test Identifier | Objective | Debug Result | Release Result |
|---|---|---|:---:|:---:|
| **Group A: Metadata** | `A1_MetadataRoundTrip` | Round-trip description, creator, topology strings | PASS | PASS |
| | `A2_CoreDirectoryRoundTrip` | Round-trip core count, lifecycle states, active flags | PASS | PASS |
| **Group B: Core State** | `B1_SmpCoreStateEquality` | Full round-trip of GPRs, PC, SP_ELx, PSTATE, SIMD, EL1-3 regs, sysregs, timers | PASS | PASS |
| | `B2_SmpCoreStateCanonicalHashExact` | Bit-exact canonical FNV-1a state hash match | PASS | PASS |
| **Group C: Shared Memory** | `C1_SparseZeroChunkOmission` | Omission of all-zero 4KB chunks reduces file size | PASS | PASS |
| | `C2_SharedMemoryBitExactRestore` | Bit-exact byte comparison across full 4MB RAM image | PASS | PASS |
| **Group D: Scheduler State** | `D1_SchedulerStateRoundTrip` | Round-trip quantum $Q$, global sequence, current core, slot metrics | PASS | PASS |
| **Group E: Exclusive Monitor** | `E1_ExclusiveMonitorRoundTrip` | Round-trip reservation granules, addresses, validity | PASS | PASS |
| **Group F: Interrupt Router** | `F1_InterruptRouterRoundTrip` | Round-trip pending cross-core SGIs and SPIs in FIFO sequence | PASS | PASS |
| **Group G: Adversarial** | `G1_ValidBufferPasses` | Valid `.wsnap` v2 passes multi-stage validation | PASS | PASS |
| | `G2_TruncatedBufferRejected` | 40-byte truncated file rejected with `TruncatedFile` | PASS | PASS |
| | `G3_HeaderCrcCorruptionRejected` | Header bit flip rejected with `HeaderCrcMismatch` | PASS | PASS |
| | `G4_SectionPayloadCrcCorruptionRejected` | Payload bit flip rejected with `SectionCrcMismatch` | PASS | PASS |
| | `G5_UnsupportedVersionRejected` | Version 99 rejected with `UnsupportedVersion` | PASS | PASS |
| **Group H: Atomicity** | `H1_CaptureToFileSuccess` | Atomic temporary file creation and rename | PASS | PASS |
| | `H2_CoreCountMismatchFailsCleanly` | Core count mismatch fails restore; live target cluster untouched | PASS | PASS |
| **Group I: Deterministic Continuation** | `I_DeterministicContinuation_2Cores_10Runs` | 10 repeated runs on 2 cores yield identical schedules & hashes | PASS | PASS |
| | `I_DeterministicContinuation_4Cores_10Runs` | 10 repeated runs on 4 cores yield identical schedules & hashes | PASS | PASS |
| | `I_DeterministicContinuation_8Cores_10Runs` | 10 repeated runs on 8 cores yield identical schedules & hashes | PASS | PASS |
| **Group J: Compatibility** | `J1_SingleCoreV1Capture` | Single-core `.wsnap` v1 captures successfully | PASS | PASS |
| | `J2_SingleCoreV1InspectionVersion1` | Single-core `.wsnap` v1 correctly inspected as version 1 | PASS | PASS |
| | `J3_MandatorySmpReplayRejection` | Replay session strictly rejects cluster snapshot (`core_count > 1`) with `UnsupportedConfiguration` | PASS | PASS |

---

## 3. Test Invariant Sign-Off

- 100% of test groups passed in both Debug and Release.
- Zero memory leaks, zero assertion failures, zero unhandled exceptions.
- Deterministic continuation verified across 30 total execution runs (10 runs each on 2, 4, and 8 cores).
