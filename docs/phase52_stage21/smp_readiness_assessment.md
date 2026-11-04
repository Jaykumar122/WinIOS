# Multi-Core SMP Replay Readiness Assessment & Staged Roadmap

## 1. Formal Readiness Classification

| Subsystem Component | Readiness Classification | Current Capability & Status |
|---|---|---|
| **CPU Interpreter State Isolation** | **READY FOR PROTOTYPE** | Per-core registers, MMU contexts, and exception state are fully isolated. |
| **Shared Physical Memory Model** | **NOT READY** | Single-core flat RAM; lacking inter-core store ordering records. |
| **Memory Consistency Semantics** | **NOT READY** | ARM weak memory reordering vs host x86 TSO unmodeled. |
| **GICv3 Multi-Core Arbitration** | **PARTIALLY READY** | GICv3 supports multi-core redistributors, but lock arbitration is unrecorded. |
| **Per-Core Timers & VirtualTime** | **PARTIALLY READY** | Counters exist, but shared atomic time advances with interleaving. |
| **SNDL Inter-Core Event Schema** | **NOT READY** | Current schema (v1.0) lacks SGI, barrier, and visibility tokens. |
| **Deterministic Scheduler** | **NOT READY** | No deterministic quantum scheduler integrated into replay loop. |
| **Cluster State Checkpointing** | **NOT READY** | StateHasher covers only single CPU context; cluster hash missing. |
| **Session Isolation & Gating** | **READY FOR STRICT REPLAY** | Multi-core replay requests are strictly rejected with `UnsupportedConfiguration`. |
| **Overall Cluster Replay Verdict** | **NOT READY** | **Strictly Unsupported — Gated at Session Initialization** |

---

## 2. Comprehensive Blocker Analysis (10 Subsystem Gaps)

In accordance with `ReplayDiagnostics::assess_smp_readiness()`, ten concrete architectural blockers prevent SMP deterministic replay today:

1. **SMP-01 (SNDL Event Model)**:
   SNDL v1.0 carries only external-input events (MMIO, device interrupt, timer, counter, entropy). There are zero records for core start/stop, barrier synchronization, release/acquire, exclusive-monitor, or inter-core visibility events.
2. **SMP-02 (Inter-Core Store Ordering)**:
   No global replay order exists for stores observed by peer cores. Concurrent read/write interleaving depends on host Windows thread scheduling and lock acquisition timing.
3. **SMP-03 (Memory Visibility Modeling)**:
   Replay memory is flat RAM with unlocked single-core access. The striped concurrent mode in `Memory` provides host mutual exclusion, not an architecturally reproducible visibility timeline.
4. **SMP-04 (Shared Virtual Time Progression)**:
   `VirtualTime` uses a shared atomic physical counter incremented per stepped instruction. Across multiple cores, counter reads depend on thread race conditions.
5. **SMP-05 (GICv3 Cross-Core SGI Delivery)**:
   The GICv3 distributor uses host recursive mutexes. The relative instruction retirement boundary at which an SGI is delivered to a target core is unrecorded.
6. **SMP-06 (Cluster Checkpoint & Hash Coverage)**:
   `ReplayCheckpoint` and `ReplayStateHash` cover one CPU, one memory image, and one timer set. Cluster-wide canonical hashing does not exist.
7. **SMP-07 (Session & Cursor Model)**:
   `ReplaySession`, `ReplayScheduler`, `ReplayInputRouter`, and `ReplayEventCursor` assume a single active core.
8. **SMP-08 (Host Scheduling Determinism)**:
   The parallel SMP engine relies on host OS threads (`std::jthread`), which are subject to preemption, quantum jitter, and core migration.
9. **SMP-09 (Divergence Localization)**:
   Divergence reporting pinpoints a single core and cursor; there is no cross-core first-divergence causal analysis.
10. **SMP-10 (Empirical Validation Evidence)**:
    Zero 2-core or 4-core large recording matrices or cross-build repeatability evidence exists for multi-core replay.

---

## 3. Staged Implementation Roadmap (Future Phases)

To bring SMP deterministic replay to production readiness, future work must follow a strictly sequential 9-stage engineering roadmap:

```
[Phase 53.1: Architecture Prerequisites]
  ├── Multi-Core CPU Snapshot Schema (.wsnap v2)
  └── Cluster-Wide Canonical State Hasher
        │
        ▼
[Phase 53.2: Deterministic Scheduler]
  ├── Fixed-Quantum Round-Robin Engine (Q = 10,000 inst)
  └── Adaptive Barrier Truncation (DSB, ISB, WFI, SGI)
        │
        ▼
[Phase 53.3: Memory Ordering & Consistency]
  ├── Explicit ARMv8 Weak Memory Barrier Emulation
  └── Global Exclusive Monitor (LDXR/STXR tracking)
        │
        ▼
[Phase 53.4: Inter-Core Event Recording]
  ├── SNDL v2 Schema Definition
  └── SGI Delivery & Core Lifecycle Event Logging
        │
        ▼
[Phase 53.5: Multi-Core Replay Prototype]
  ├── Per-Core Event Cursors
  └── Coordinated ReplayInputRouter
        │
        ▼
[Phase 53.6: Adversarial Validation & Hardening]
  ├── Multi-Core Divergence Detection
  └── 10-Run Repeatability on 2-Core / 4-Core Workloads
        │
        ▼
[Phase 53.7: SMP Production Readiness Sign-Off]
```

Until all nine stages are completed and verified by empirical evidence, SMP replay will remain **strictly gated and rejected**.
