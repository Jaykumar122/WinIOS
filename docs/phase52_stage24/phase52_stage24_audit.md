# Phase 52, Stage 24: Initial Architecture Audit & Gap Analysis

## 1. Executive Summary
This document establishes the pre-implementation baseline and gap analysis for **Phase 52, Stage 24: Multi-Core SNDL v2 Recording Architecture, Global Event Ordering & Deterministic Event Capture**.

Prior to Stage 24, WinIOS possessed:
- Single-host-thread deterministic SMP foundation (`SmpScheduler`, `SmpCoreState`, `SmpSharedMemory`, `SmpExclusiveMonitorManager`, `SmpInterruptRouter`).
- Cluster-wide multi-core snapshot serialization (`.wsnap v2`).
- Hardened single-core deterministic replay engine (`.wsnap v1` + `.sndl v1`).
- Diagnostic execution tracing (`.wtr v1`).

However, multi-core non-deterministic input recording remained **NOT READY**.

---

## 2. Existing SNDL v1 Architecture Audit
Inspection of `D:\WinISO26\WinIOS\src\sndl\` reveals:
- Magic identifier: `kWSndlMagic = {'W', 'S', 'N', 'D', 'L', '\0', '\1', '\0'}` (Version 1).
- Header: `WSndlHeader` (64 bytes), contains `version = 1`, `core_count` (fixed to 1 in replay engine), `base_snapshot_hash`, `total_record_count`.
- Record schema: `SndlRecord` (64 bytes), fields: `sequence_num`, `instruction_retirement_count`, `pc`, `payload1`, `payload2`, `payload3`, `source_id`, `flags`, `core_id`, `sub_instruction_ordinal`, `event_type`, `boundary_type`, `payload_size`.
- Block header: `WSndlBlockHeader` (48 bytes), magic `'S', 'B', 'L', 'K'`.
- Footer: `WSndlFooter` (64 bytes), magic `'W', 'S', 'N', 'D', 'L', 'F', 'T', 'R'`.

### Gap Analysis for SNDL v1:
1. **Single-Core Bias**: v1 assumes single-threaded instruction retirement without tracking global retirement sequence across cores.
2. **Missing Multi-Core Event Types**: v1 event registry lacks scheduler quantum events (`QuantumStart`, `QuantumEnd`, `EarlyTruncation`, `CoreSelected`, `BarrierEntry`, `BarrierRelease`).
3. **Missing Cross-Core Interrupt Events**: No explicit distinction between GIC routing and per-core accepted interrupt delivery.
4. **No Cluster Snapshot Binding**: v1 binds to single-core `.wsnap v1` snapshots, unable to validate `.wsnap v2` cluster snapshots.

---

## 3. SMP Architecture Audit
Inspection of `D:\WinISO26\WinIOS\src\cpu\`:
- `SmpScheduler`: Coordinates round-robin scheduling of virtual cores with configurable instruction quantum $Q$. Maintains a monotonic `global_retirement_sequence_` and audit transition history.
- `SmpSharedMemory`: Single-threaded host backing store for physical guest RAM, with attached `SmpExclusiveMonitorManager`.
- `SmpExclusiveMonitorManager`: Per-core reservation tracking for load-exclusive / store-exclusive pairs.
- `SmpInterruptRouter`: Cross-core interrupt routing with FIFO delivery queues.

### Gap Analysis for SMP Recording:
- `SmpScheduler` executed instructions without an attached recording observer.
- No facility existed to stream multi-core scheduling boundaries to disk without perturbing architectural execution.

---

## 4. Replay Guardrail Audit
Inspection of `D:\WinISO26\WinIOS\src\replay\replay_session.cpp`:
- Lines 63-72: Explicit rejection of multi-core configurations (`mode == ReplayMode::MultiCore && !allow_experimental_smp`).
- Lines 76-94: Early header inspection of snapshot files rejecting `core_count > 1` with `ReplayErrorCode::UnsupportedConfiguration`.

### Guardrail Mandate:
Stage 24 MUST NOT weaken, bypass, or alter these checks. Multi-core replay remains strictly NOT READY.
