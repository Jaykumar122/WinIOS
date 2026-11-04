# Four-Core Shared Memory Validation

## 1. Architectural Shared RAM Model

In WinIOS, `SmpSharedMemory` manages guest physical RAM accessible by all cores. Under single-host-thread scheduling, concurrent access from multiple virtual cores executes as a discrete, deterministic sequence of memory loads and stores determined by:
1. Core scheduling sequence ($0 \rightarrow 1 \rightarrow 2 \rightarrow 3$).
2. Configured quantum slice bounds ($Q$).
3. Explicit barrier synchronizations (`DMB`, `DSB`, `ISB`).

---

## 2. Validation Workload: Disjoint and Interleaved Pages

In `test_group_7_shared_memory_determinism`:
- Four cores write distinct patterns into distinct memory pages (offsets `0x4000`, `0x4100`, `0x4200`, `0x4300`).
- Slices alternate across cores every 10 instructions.
- Upon completion, the canonical cluster hash computes the full CRC32C over the shared memory buffer.

### Verification Results
- 10 consecutive executions produce identical CRC32C checksums of shared RAM.
- Byte-level memory comparison confirms zero discrepancy across runs.
- Disclaimer: Deterministic interleaving reflects WinIOS single-host-thread scheduler semantics and does not constitute full ARMv8 weak memory model hardware conformance.
