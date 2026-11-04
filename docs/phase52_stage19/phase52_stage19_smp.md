# WinIOS Phase 52, Stage 19 — Multi-Core (SMP) Replay Analysis

## 1. Status: Unsupported / Experimental

In accordance with Phase 52 Stage 19 requirements, **multi-core SMP replay is explicitly classified as UNSUPPORTED and EXPERIMENTAL**.

Attempting to run `ReplaySession` or `ReplayEngine` with `ReplayMode::MultiCore` without setting `allow_experimental_smp = true` returns an immediate, deterministic error:

```cpp
ReplayErrorCode::UnsupportedConfiguration
"Multi-core SMP replay is unsupported/experimental due to uncontrolled "
"thread scheduling and missing cross-core memory visibility guarantees."
```

---

## 2. Technical Root Causes of Multi-Core Non-Determinism

WinIOS models multi-core execution through `SMPCluster` (`src/cpu/smp_cluster.cpp`), where secondary cores are launched as concurrent host worker threads running alongside the primary core. While instruction execution on each individual core is deterministic in isolation, the interleaving across cores is inherently non-deterministic.

```
       Core 0 Worker Thread                  Core 1 Worker Thread
       (Host Thread A)                       (Host Thread B)
              |                                     |
              v                                     v
       Store X1 -> [Addr Z]                  Load X2 <- [Addr Z]
              \                                     /
               \                                   /
                +---------------------------------+
                | Host OS Thread Scheduler Jitter |
                | Host Memory Subsystem Reorder   |
                +---------------------------------+
                                |
             +------------------+------------------+
             |                                     |
      Case A: Core 0 writes FIRST           Case B: Core 1 reads FIRST
      Core 1 loads new value                Core 1 loads old value
      ==> Determinism Preserved             ==> Divergence Occurs!
```

### 2.1 Uncontrolled Memory Visibility Interleaving

AArch64 specifies a weakly-ordered memory model. When multiple guest cores concurrently read and write shared guest RAM:
1. The relative arrival order of stores to the shared `Memory` object is subject to host thread preemption, host CPU cache coherency protocol latency, and OS scheduling jitter.
2. In the absence of a complete global memory access trace or deterministic multithreading (DMT) runtime, it is impossible to reconstruct the exact interleaving of shared-memory loads from other cores.

### 2.2 Lack of Cross-Core Synchronization Logging

The `.sndl` schema captures input boundaries (MMIO, external interrupts, cycle counters, entropy). However, standard memory loads and stores between cores in shared RAM are **not** logged in `.sndl` to keep log size overhead manageable (logging every shared-memory load would produce hundreds of megabytes per second of trace data).

Without logging every shared-memory read or synchronizing core progress with deterministic logical clock ticks, identical execution order across cores cannot be guaranteed.

### 2.3 Exclusive Monitor Interference Across Host Threads

The AArch64 exclusive reservation architecture (`LDXR`, `STXR`) tracks reservations:
- In multi-threaded mode, an atomic store on Core 1 invalidates the reservation on Core 0.
- A slight timing drift between threads causes an `STXR` to succeed in one run (returning status 0) and fail in another run (returning status 1), resulting in branch divergences in spinlocks, mutexes, and lock-free queues.

---

## 3. Requirements for Future Deterministic Multi-Core Support

To transition multi-core replay from experimental to fully supported in future phases, WinIOS requires:

1. **Deterministic Multithreading (DMT)**:
   - Enforce fixed logical execution epochs (e.g. Round-Robin core stepping, or fixed quantum instruction slicing per core).
   - Guarantee that Core 0 executes $K$ instructions, followed by Core 1 executing $K$ instructions, ensuring memory interleaving is purely a function of instruction retirement counts.
2. **Memory Barrier and Synchronization Logging**:
   - Record the outcome and sequence of all acquire/release barriers (`DMB`, `DSB`, `ISB`) and exclusive monitor operations (`LDXR`/`STXR`).
3. **Lamport Logical Clocks**:
   - Establish a total causal ordering of cross-core PSCI events and inter-processor interrupts (IPI / SGIs).
