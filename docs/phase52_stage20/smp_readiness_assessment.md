# Multi-Core SMP Replay Readiness Assessment

## 1. Formal Readiness Classification

| Evaluation Target | Readiness Verdict | Replay Status | Rejection Policy |
|---|---|---|---|
| **Single-Core Replay** | **PRODUCTION READY** | Fully Deterministic (10/10 runs match) | Enabled |
| **Multi-Core SMP Replay** | **NOT READY** | **UNSUPPORTED** | **Strictly Rejected at Session Preparation** |

> [!CAUTION]
> **Definitive Finding**: Multi-core symmetric multiprocessing (SMP) deterministic replay is **NOT READY** for production use. Any claim of general SMP replay capability under the current architecture would be factually incorrect and technically unsound. Replay requests specifying `core_count > 1` are actively rejected with `ReplayErrorCode::UnsupportedConfiguration` and `ReplayCompletionStatus::UnsupportedConfiguration`.

---

## 2. Technical Impediments to SMP Replay

Deterministic replay of a single-core system requires capturing only inputs that cross the CPU core boundary (MMIO, interrupts, timers, entropy). In contrast, deterministic replay of an SMP system requires capturing the exact memory interleaving of concurrent cores or enforcing deterministic scheduling. The following four fundamental technical gaps prevent SMP replay today:

### Gap 1: Shared Memory Access Interleaving & Data Races
In an SMP guest, multiple cores execute in parallel against shared physical memory. When Core 0 writes to address $A$ and Core 1 reads from address $A$:
- If Core 1 reads *before* Core 0 writes, Core 1 observes old data $D_0$.
- If Core 1 reads *after* Core 0 writes, Core 1 observes new data $D_1$.

The relative ordering of these instructions depends entirely on nanosecond-level host thread execution speeds. Without recording every shared memory access interleaving or enforcing a coarse-grained memory ownership protocol (such as CREW - Concurrent Read, Exclusive Write page protection), replaying the two cores in separate threads inevitably leads to memory races, divergent register states, and cascading system failure.

### Gap 2: Memory Consistency Model Mismatch (ARM Weak vs Host x86 TSO)
- **Guest Architecture**: ARMv8-A implements a Weak Memory Ordering model allowing aggressive load-load, load-store, and store-store reordering, controlled only by explicit barrier instructions (`DMB`, `DSB`, `ISB`) and acquire-release semantics (`LDAR`, `STLR`).
- **Host Architecture**: The host test environment runs on x86-64, which implements Total Store Order (TSO), preventing most load-load and store-store reorderings.
- Emulating ARM weak memory semantics deterministically across multiple host worker threads without hardware assistance introduces immense overhead (up to $50\times$ slowdown) or nondeterministic reordering anomalies.

### Gap 3: Host OS Thread Scheduling & Preemption Jitter
On a Windows host, guest virtual CPU cores are executed on native OS threads (`std::jthread` / Windows thread pool). Native threads are subjected to:
- Windows kernel thread scheduler quanta and preemptive context switching.
- Thread priority boosts and thermal throttling variations.
- SMT / hyperthreading execution pipeline contention.
- Dynamic CPU core migration across host NUMA nodes.

Because instruction execution rates fluctuate unpredictably between threads, the relative instruction retirement skew between Core 0 and Core 1 drifts continuously, destroying deterministic event delivery timing.

### Gap 4: GICv3 Cross-Core SGI & Lock Contention
When Core 0 dispatches an inter-processor interrupt via Software Generated Interrupt register `ICC_SGI1R_EL1` targeting Core 1:
- The exact instruction on Core 1 at which the SGI is delivered depends on the relative phase between the two host threads.
- GIC distributor mutex arbitration introduces nondeterministic thread lock acquisition ordering.

---

## 3. Current Rejection Enforcement (Test Group K)

To protect system integrity, WinIOS strictly enforces SMP rejection at the API boundary:

```cpp
// In ReplaySession::prepare():
if (session_config_.mode == ReplayMode::MultiCore || session_config_.core_count > 1) {
    if (!session_config_.allow_experimental_smp) {
        stats_.completion_status = ReplayCompletionStatus::UnsupportedConfiguration;
        return ReplayResult::failure(
            ReplayErrorCode::UnsupportedConfiguration,
            "Multi-core SMP replay is not supported in Phase 52 Stage 20"
        );
    }
}
```

Test Group K (`test_group_k_smp_rejection` in `tests/unit/replay_hardening_tests.cpp`) verifies that any attempt to initialize an SMP replay session returns failure with code `UnsupportedConfiguration` and records `ReplayCompletionStatus::UnsupportedConfiguration`.

---

## 4. Architectural Roadmap for Future SMP Deterministic Replay

Achieving full SMP deterministic replay in future phases will require four substantial infrastructure developments:

1. **Deterministic Multi-Threading (DMT) Engine**:
   - Time-slice quantum scheduling (e.g. each core executes exactly 10,000 instructions in a strict round-robin sequence).
   - Global barrier synchronization at quantum boundaries.
2. **Page-Table CREW (Concurrent Read, Exclusive Write) Protection**:
   - Utilizing page-fault traps (`VirtualProtect` / MMU PTE dirty tracking) to detect shared-memory ownership transfers between cores.
3. **Memory Access Interleaving Log in SNDL**:
   - Recording shared-memory write tokens and synchronization order into a multi-stream `.sndl` format.
4. **Hardware Performance Counter Instruction Retires (PMU) Sync**:
   - Synchronizing cores based on precise retired instruction counts rather than wall-clock time.
