# WinIOS Campaign 197 Stage 197.12: Host-Guest Synchronization Boundary Audit

**Document ID:** `docs/campaign197/host_guest_synchronization_boundary.md`  
**Classification:** `HOST_MODEL` vs `GUEST_MEMORY_MODEL`  
**Acceptance Test:** Acceptance Test I (Host synchronization is not mistaken for guest architectural behavior)

---

## 1. Executive Summary & Problem Definition

WinIOS executes on a Windows 11 x86-64 host. The host hardware enforces Total Store Order (TSO), which guarantees:
- Loads are not reordered with other loads.
- Stores are not reordered with older loads or stores.
- Store-to-load forwarding is allowed, but stores are globally visible in total order.

In contrast, the guest target architecture is ARMv8-A (A16 Bionic / Apple Silicon), which is a **relaxed, weakly-ordered memory architecture**:
- Load-Load, Load-Store, Store-Store, and Store-Load reorderings are permitted unless explicitly ordered by barriers (`DMB`), acquire-release instructions (`LDAR`/`STLR`), or data/address dependencies.
- Multi-copy atomicity is other-multicopy atomic (OMCA), meaning writes become visible to all other cores simultaneously once committed.

If a developer mistakenly equates host synchronization (`std::mutex`, `std::atomic`, `std::atomic_thread_fence`) with guest architectural behavior, the emulator would silently provide x86 TSO semantics to the guest, masking real ARM concurrency bugs.

This document audits and classifies every synchronization primitive in the WinIOS codebase, establishing a strict conceptual and architectural boundary between **Host Platform Concurrency Safety** and **Guest Architectural Memory Simulation**.

---

## 2. Synchronization Boundary Classification Taxonomy

Every synchronization primitive in WinIOS falls into one of two mutually exclusive categories:

1. **Host-Level Infrastructure Concurrency (`HOST_INFRASTRUCTURE`):**
   - **Purpose:** Protects host C++ memory safety, prevents C++ data races, synchronizes host worker threads, and coordinates simulator thread pools.
   - **Architectural Visibility to Guest:** Invisible. Guest instructions do not perceive host mutex acquisitions, spinlocks, or conditional variables.
   - **Examples:** `SMPCluster::reservations_mutex_`, `GuestMemoryOrderModel::mutex_`, `TraceManager::mutex_`.

2. **Guest-Level Architectural Simulation (`GUEST_ARCHITECTURAL_MODEL`):**
   - **Purpose:** Evaluates whether guest memory accesses adhere to ARMv8-A DDI 0487 axiomatic rules, regardless of host ordering.
   - **Mechanism:** Implemented in `GuestMemoryOrderModel` via per-core program-order sequence generation, store buffering simulation, barrier history logs, and litmus evaluation verdicts (`Allowed` vs `Forbidden`).
   - **Examples:** `CoreOrderingState::queue_store()`, `CoreOrderingState::note_dmb()`, `GuestMemoryOrderModel::is_ordered()`.

---

## 3. Comprehensive Audit of Host Synchronization Primitives

| Component / File | Primitive | Classification | Architectural Role & Justification |
|---|---|---|---|
| `src/cpu/guest_memory_order.h` | `std::mutex mutex_` | `HOST_INFRASTRUCTURE` | Protects centralized litmus event logs and per-core states from host-side memory corruption when multiple worker threads record accesses. |
| `src/cpu/smp_cluster.h` | `std::mutex reservations_mutex_` | `HOST_INFRASTRUCTURE` | Protects global reservation vector across host threads during concurrent exclusive loads/stores. |
| `src/cpu/smp_cluster.h` | `std::recursive_mutex store_mutex_` | `HOST_INFRASTRUCTURE` | Serializes guest store execution across host emulator threads to ensure host memory writes are atomic with respect to each other. |
| `src/cpu/smp_cluster.h` | `std::atomic<bool> parallel_started_` | `HOST_INFRASTRUCTURE` | Host-side atomic flag indicating whether parallel emulator worker threads are actively executing. |
| `src/cpu/smp_exclusive_monitor.h` | `mutable SmpExclusiveMonitorTelemetry telemetry_` | `HOST_INFRASTRUCTURE` | Non-architectural performance telemetry counter tracking reservations, invalidations, and CLREX operations. |
| `src/cpu/cpu.cpp` | `std::atomic_thread_fence(std::memory_order_seq_cst)` in DSB | `HOST_INFRASTRUCTURE` | Host memory fence ensuring that when a guest DSB executes, the host CPU also drains pending host write buffers to prevent host out-of-order execution. |
| `src/cpu/cpu.cpp` | `std::atomic_thread_fence(std::memory_order_release)` in STLR | `HOST_INFRASTRUCTURE` | Synchronizes host memory writes across host cores executing different virtual cores. |
| `src/cpu/cpu.cpp` | `std::atomic_thread_fence(std::memory_order_acquire)` in LDAR | `HOST_INFRASTRUCTURE` | Ensures host compiler and host CPU do not hoist subsequent host reads before the LDAR. |
| `src/cpu/cpu.cpp` | `GuestMemoryOrderModel::record_*()` | `GUEST_ARCHITECTURAL_MODEL` | **True Guest Architectural Model:** Tracks guest program order sequence numbers, stores, loads, barriers, and evaluates ARMv8-A weak-memory conformance. |

---

## 4. Isolation Guarantee

1. **Non-Conflation:** WinIOS never asserts that host memory ordering satisfies guest ARM requirements.
2. **Explicit Weak-Memory Simulation:** The test suite verifies weak-memory outcomes (e.g. `SB` allowed without barriers, `MP` allowed without barriers) via `GuestMemoryOrderModel`, confirming that the host's stronger TSO ordering does not distort the guest's weak-memory simulation.
3. **Audit Verdict:** The host-guest synchronization boundary is strictly enforced and decoupled.
