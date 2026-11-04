# SNDL v2 Multi-Core Recording Architecture

## 1. Overview
The Snapshot + Non-Deterministic Log (SNDL) architecture decouples deterministic execution playback from diagnostic tracing.
While SNDL v1 supported only single-core execution with a single retirement stream, SNDL v2 introduces:
1. Multi-core virtual cluster support ($1 \le \text{core\_count} \le 256$).
2. Dual sequence clocks: `global_event_sequence` (strictly monotonic ordering of all recorded events) and `global_retirement_sequence` (cluster-wide instruction retirement count).
3. Per-core sequence tracking: `core_retirement_count` maintaining monotonic execution boundaries per core.
4. Comprehensive multi-core event registry covering scheduler transitions, cross-core interrupts, architectural timers, MMIO, entropy, and exclusive monitors.
5. Baseline snapshot binding to `.wsnap v2` cluster snapshots via canonical state hashing.

---

## 2. Decoupling Recording from Replay
Stage 24 strictly addresses **event recording and validation**.
A common architectural failure mode in virtual machine development is implementing recording and replay simultaneously, leading to hidden coupling and fragile replay shortcuts.
In WinIOS:
- **Recording Engine (`SndlV2Recorder`)**: Observes execution non-invasively, enforces FIFO sequencing, batches records into blocks, and emits signed binary streams.
- **Validation Engine (`SndlV2Validator`)**: Performs independent streaming verification of binary integrity, CRC32C checksums, monotonic sequences, and snapshot compatibility.
- **Replay Safety Guard**: Replay remains explicitly disabled for multi-core configurations (`ReplayErrorCode::UnsupportedConfiguration`). Recording validation proves input completeness and sequence integrity without claiming replay readiness.

---

## 3. Producer-Consumer Execution Contract
Under the current Stage 22/24 SMP design:
- The execution engine operates on a single host thread (`SmpScheduler`), executing instruction slices across cores in strict round-robin order.
- The `SndlV2Recorder` operates as a single-producer observer directly attached to `SmpScheduler`.
- Race conditions, concurrent lock contention, and out-of-order event delivery are architecturally eliminated.
- Multi-threaded host producers are explicitly out of scope for Stage 24 and documented as unsupported.
