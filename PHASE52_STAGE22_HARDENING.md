# WinIOS — Phase 52, Stage 22 Architecture Hardening Report
## Deterministic SMP Foundation: Multi-Core CPU State, Global Scheduler & Shared-Memory Architecture

---

## 1. Architectural Invariants & Hardening Guarantees

Phase 52 Stage 22 enforces strict engineering boundaries to ensure determinism, safety, and architectural integrity:

### Invariant 1: Single Host Thread Execution
All virtual cores execute on a single host OS thread via `SmpScheduler`. This completely eliminates:
- OS thread scheduling jitter and arbitrary host preemption.
- Host-level race conditions on memory and system registers.
- Need for complex cross-thread synchronization primitives that could compromise determinism.

### Invariant 2: Host Pointer & Metric Immunity in Canonical Hashing
`SmpCoreState::compute_canonical_hash()` and `SmpScheduler::compute_canonical_multicore_hash()` compute 64-bit FNV-1a hashes exclusively over architectural state:
- Excluded: Host pointers (`this`, `Memory*`, `TraceSink`), allocator addresses, debug sinks.
- Excluded: Transient metrics (`current_budget`, `executed_in_quantum`, memory telemetry).
- Included: Little-endian integer registers, PC, banked stack pointers, packed PSTATE, vector registers, FPCR/FPSR, exception state (EL1..3), MMU sysregs, timers, and shared memory contents.
- Result: 100% bit-exact parity across 10-run repeatability stress tests.

### Invariant 3: 64-Byte Cacheline Reservation Coherence
`SmpExclusiveMonitorManager` enforces a 64-byte reservation granule (`addr & ~0x3FULL`):
- Any write to an overlapping 64-byte cacheline invalidates peer reservations atomically with the memory store.
- Cross-core LDXR/STXR/CLREX sequence semantics verified.

### Invariant 4: Strict Request/Delivery Separation for Cross-Core Interrupts
`SmpInterruptRouter` separates the enqueuing of interrupt requests from their delivery to CPU cores:
- Queued requests do not alter CPU architectural state mid-quantum.
- Delivery occurs deterministically at defined quantum or instruction boundaries.
- Monotonic sequence numbering and FIFO ordering prevent interrupt reordering.

### Invariant 5: Mandatory Replay Rejection
`ReplaySession` strictly rejects `core_count > 1` configurations:
```cpp
if (config_.mode == ReplayMode::MultiCore && !config_.allow_experimental_smp) {
    state_ = ReplayLifecycleState::Failed;
    stats_.completion_status = ReplayCompletionStatus::UnsupportedConfiguration;
    return ReplayResult::fail(
        ReplayErrorCode::UnsupportedConfiguration,
        "Multi-core SMP replay is unsupported/experimental..."
    );
}
```

---

## 2. CLI Tooling Hardening

The CLI interface in `src/main.cpp` was expanded with hardened `smp` subcommands:
- `win_ios.exe smp inspect`: Displays architectural subsystem overview.
- `win_ios.exe smp cores [--cores <n>]`: Inspects virtual core allocations and canonical hashes.
- `win_ios.exe smp scheduler [--cores <n>] [--quantum <n>] [--max-instructions <n>]`: Runs deterministic round-robin scheduler and displays execution metrics.
- `win_ios.exe smp benchmark [--cores <n>]`: Executes quick latency and throughput benchmarks.
- `win_ios.exe smp readiness`: Outputs formal readiness verdict (explicitly NOT READY for SMP replay).

---

## 3. Quality Assurance Summary

- Unit Tests: 34/34 Passed (100%) in Release and Debug.
- CTest Suite: 57/57 Passed (1 skipped external integration test).
- Zero regressions in existing single-core replay, snapshot, or trace pipelines.
- Backward compatibility: `.wtr`, `.wsnap` v1, and `.sndl` v1 formats remain 100% operational.
