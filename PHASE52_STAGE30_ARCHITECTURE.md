# WinIOS Phase 52 Stage 30 Architecture Document
## Long-Duration Replay Hardening, Cross-Topology Coordination & Resilience

### 1. Architectural Scope and Design Objectives
Phase 52 Stage 30 hardens the multicore deterministic replay architecture of WinIOS. It establishes long-duration operational stability (up to 10M instructions), cross-topology compatibility (1, 2, 4, 8 cores), analytical expected-state ground truth validation, memory footprint bounds under large snapshots, and deterministic event recovery under adversarial corruption.

### 2. Core Subsystems

#### 2.1 Single-Host-Thread Deterministic Scheduler
- Round-robin quantum interleaving across active virtual cores.
- Monotonic global instruction retirement sequence counter (`global_retirement_sequence`).
- Core-specific retirement counters (`core_retirement_count[core_id]`).
- Clean handling of idle states: Cores in `WaitingForInterrupt` (WFI) or `WaitingForEvent` (WFE) are skipped without wasting host cycles or altering instruction accounts.

#### 2.2 SmpReplayCoordinator & Replay Engine
- Coordinates snapshot restoration and SNDL v2 event consumption.
- Dual capability gates for 8-core replay:
  - `allow_experimental_smp = true`
  - `allow_eight_core_replay = true`
- Topology verification: Ensures that snapshot core count, recording core count, and coordinator core count match identically. Mismatches abort before any guest execution.
- Capability protection: `allow_uncaptured_dma = false` is strictly enforced.

#### 2.3 SNDL v2 Recording and Ingestion Pipeline
- Stream-based block format (`kDefaultV2RecordsPerBlock = 512`, queue capacity 65,536).
- Strict event type validation: MMIO read, entropy, external interrupt, timer expiration, lifecycle transition.
- Deterministic tie-breaking for simultaneous virtual timer expirations via ascending core ID ordering.
- Adversarial integrity checks: Rejection of corrupted magic, truncated payloads, trailing bytes, or aborted footers with structured `SmpReplayStatus` return codes.

#### 2.4 Canonical Cluster State Hashing
- MurmurHash3-64 computed across:
  - All 31 64-bit general-purpose registers per core ($X0$–$X30$).
  - Program counter ($PC$) and stack pointers ($SP\_EL0$ through $SP\_EL3$).
  - Processor state flags ($PSTATE$, $NZCV$, $DAIF$, current exception level).
  - Floating-point and NEON vector registers ($V0$–$V31$, $FPCR$, $FPSR$).
  - Shared physical memory contents.
- Snapshot base hash verification and final state hash invariant check.

### 3. Verification and Hardening Matrix
- 8 newly developed unit test and profiling suites covering long-duration workloads, cross-topology combinations, analytical expected-state validation, corruption recovery, large snapshots (16 MB and 32 MB), scheduler fairness, interrupt storms, and performance profiling.
- Total test count: 75 CTest targets (74 passed, 1 skipped, 0 failed).
