# Stage 197.2 — Guest Memory Ordering Architecture

## 1. Architectural Scope & Problem Statement
On a Windows x64 host, all memory accesses inherit the strong Total Store Order (TSO) hardware guarantees of x86-64:
- Stores are not reordered with other stores (`Store-Store` ordering preserved).
- Loads are not reordered with other loads (`Load-Load` ordering preserved).
- Loads are not reordered with prior stores to the same address (`Store-Load` forwarding).

In contrast, ARMv8-A architecture (ARM DDI 0487 Section B2) is a weakly ordered multi-copy atomic memory model:
- Explicit barriers (`DMB`, `DSB`) or one-way synchronization instructions (`LDAR`, `STLR`) are required to enforce ordering between independent memory locations.
- Prior to Campaign 197, WinIOS relied on host C++ fences (`std::atomic_thread_fence`) to serialize threads on the host CPU. This prevented litmus test validation of permitted weak-memory behaviors (e.g. Store Buffering where both cores observe stale values simultaneously).

Stage 197.2 introduces the **Guest Memory Ordering Architecture** (`src/cpu/guest_memory_order.h` and `src/cpu/guest_memory_order.cpp`), which establishes an explicit, guest-visible ordering model independent of host CPU hardware behavior.

---

## 2. Core Architectural Components

### 2.1 MemoryAccessEvent
Represents an individual guest memory transaction or barrier:
- `core_id`: Originating virtual CPU core.
- `program_order_seq`: Monotonically increasing per-core sequence number defining Program Order (po).
- `global_commit_seq`: Monotonically increasing cluster-wide sequence number defining Global Observation Order.
- `address`, `size`, `value`: Architectural memory location and access width.
- `type`: `NormalRead`, `NormalWrite`, `AcquireRead`, `ReleaseWrite`, `ExclusiveRead`, `ExclusiveWrite`, `AtomicRMW`.
- `barrier`: `DMB`, `DSB`, `ISB`, or `None`.
- `barrier_opt`: Decoded domain (`InnerShareable`, `OuterShareable`, `NonShareable`, `FullSystem`) and access type (`Reads`, `Writes`, `All`).

### 2.2 CoreOrderingState & Store Buffer
Each virtual CPU core maintains:
- `next_po_seq_`: Local program order sequence generator.
- `store_buffer_`: FIFO queue of issued stores that are visible to the issuing core via store-to-load forwarding, but not yet committed to global memory.
- `history_`: Complete audit log of memory events for post-execution trace validation and deterministic replay.
- `note_dmb()`, `note_dsb()`, `note_isb()`: Records synchronization events and tracks pipeline flushes.

### 2.3 GuestMemoryOrderModel
Cluster-level coordinator that:
1. Translates CPU load/store/barrier execution into tracked events.
2. Evaluates pairwise ordering via `is_ordered(event_a, event_b)`:
   - Preserves intra-core coherence order on identical addresses (`a.address == b.address`).
   - Enforces Release-Acquire synchronizes-with edges across cores.
   - Enforces DMB barriers according to domain and access options.
   - Enforces DSB completion semantics: drains pending store buffers before subsequent instructions proceed.
   - Enforces ISB pipeline synchronization and context invalidation.
3. Provides axiomatic litmus outcome verification:
   - `evaluate_sb_outcome`: Permits Store Buffering weak outcomes `(0, 0)`.
   - `evaluate_mp_outcome`: Forbids Message Passing race `(flag == 1, data == 0)` when guarded by release-acquire.
   - `evaluate_lb_outcome`: Forbids Load Buffering cycle `(1, 1)` due to causality.
   - `evaluate_iriw_outcome`: Forbids contradictory observation orders across independent observers under multi-copy atomicity.
4. Generates a canonical 64-bit FNV-1a ordering state hash for deterministic replay validation.

---

## 3. Verification & Acceptance
The test suite `stage197_2_guest_memory_order_tests` exercises all six foundational capabilities:
1. Program order sequence generation and per-core event tracking.
2. Store buffer queueing, store-to-load forwarding, and deferred visibility.
3. Release-Acquire ordering enforcement across cores.
4. Barrier ordering decisions across DMB, DSB, and ISB.
5. Axiomatic litmus outcome verdicts (Allowed vs Forbidden).
6. Bit-exact deterministic state hashing across identical execution runs.
