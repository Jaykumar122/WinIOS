# SMP Memory Ordering & Consistency Model Analysis

## 1. Architectural Divergence: ARMv8 Weak Memory vs. Host x86 TSO

The fundamental obstacle to deterministic SMP virtualization across heterogeneous host architectures is the discrepancy between memory consistency models:

| Architectural Property | Guest Model: ARMv8-A Weak Memory | Host Model: x86-64 Total Store Order (TSO) | Replay Hazard |
|---|---|---|---|
| **Load $\rightarrow$ Load Reordering** | **Permitted** (unless address-dependent or barrier) | **Forbidden** (program order preserved) | Guest code may assume reordering is possible; host executes sequentially. |
| **Store $\rightarrow$ Store Reordering** | **Permitted** (unless barrier / STLR) | **Forbidden** (program order preserved) | Host thread enforces store queue order; hides guest concurrency bugs. |
| **Load $\rightarrow$ Store Reordering** | **Permitted** | **Forbidden** | Host forbids early store execution. |
| **Store $\rightarrow$ Load Reordering** | **Permitted** | **Permitted** (via store buffer forwarding) | Only common reordering between guest and host. |
| **Multi-Copy Atomicity** | **Other-Multi-Copy Atomic** (ARMv8-A) | **Total Multi-Copy Atomic** | A store becomes visible to all other cores simultaneously on ARMv8. |

> [!WARNING]
> **Host TSO Masking Hazard**: Executing ARMv8 SMP code directly on an x86-64 host with native memory loads/stores masks memory race conditions because x86-64 hardware enforces TSO. However, because host thread scheduling interleaving varies unpredictably between recording and replay, memory observations will still diverge unless access interleaving is strictly controlled.

---

## 2. ARMv8 Memory Barrier Modeling in WinIOS

To achieve deterministic memory behavior across cores, WinIOS must explicitly model the four architectural barrier mechanisms:

### 1. Data Memory Barrier (`DMB`)
`DMB` ensures that all memory accesses appearing in program order before the barrier are observed by other observers before any memory accesses appearing in program order after the barrier:
- `DMB ISHLD`: Inner Shareable Load-Load and Load-Store ordering.
- `DMB ISHST`: Inner Shareable Store-Store ordering.
- `DMB ISH` / `SY`: Full bidirectional barrier across all memory operations.

### 2. Data Synchronization Barrier (`DSB`)
`DSB` enforces memory ordering and stalls instruction retirement on the current core until all memory accesses (and translation table walks / cache maintenance operations) have reached visibility.

### 3. Instruction Synchronization Barrier (`ISB`)
`ISB` flushes the CPU fetch pipeline and re-fetches instructions from the current program counter, ensuring that self-modifying code or MMU configuration changes (`SCTLR_EL1`, `TTBR0_EL1`) are architecturally visible.

### 4. Acquire-Release Semantics (`LDAR` / `STLR`)
- **`LDAR` (Load-Acquire)**: Generates a one-way memory barrier preventing subsequent reads and writes from being reordered before the load.
- **`STLR` (Store-Release)**: Generates a one-way memory barrier preventing prior reads and writes from being reordered after the store.

---

## 3. Exclusive Monitor State Machine (LL/SC)

ARMv8 implements atomic read-modify-write sequences using Load-Exclusive (`LDXR`) and Store-Exclusive (`STXR`):

```
       [Core A: LDXR X0, [Addr]]
                  │
                  ▼
       [Local Monitor: Open] ──► [Global Monitor: Tagged(Core A, Addr)]
                  │
        ┌─────────┴─────────┐
        │                   │
        ▼                   ▼
[Core B: STR [Addr]]    [Core A: STXR W1, X2, [Addr]]
        │                   │
        ▼                   ▼
[Global Monitor: Cleared]  [Monitor Checked]
                            ├── Tagged Core A? ──► Success (W1=0, Store Committed)
                            └── Cleared?       ──► Failure (W1=1, Store Aborted)
```

### Deterministic Modeling Requirements
- In single-core execution, a local boolean flag suffices: any write clears the monitor.
- In SMP replay, the global monitor must track `(core_id, address_tag)` cluster-wide. An inter-core store from Core $B$ must record its monitor-clearing effect at a precise instruction boundary on Core $A$.

---

## 4. Atomic Memory Operations (ARMv8.1-A LSE)

ARMv8.1-A introduces single-instruction atomic operations:
- `LDADD`, `LDCLR`, `LDEOR`, `LDSET` (Atomic Fetch-and-Op).
- `SWP` (Atomic Swap).
- `CAS` / `CASP` (Compare-and-Swap Single and Pair).

Unlike `LDXR`/`STXR` loops, LSE atomics perform memory read, ALU operation, and write in a single atomic cycle. During deterministic replay, LSE instructions must be serialized through a cluster-wide memory token so that concurrent atomics across cores resolve in identical order.
