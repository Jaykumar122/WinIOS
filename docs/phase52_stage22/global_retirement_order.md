# Global Instruction Retirement Sequence & Order

## 1. Architectural Scope

In a multi-core system, each virtual core maintains its own local instruction retirement counter:
$$C_i = \sum \text{instructions retired on Core } i$$

However, to provide a single, totally-ordered timeline for auditing, event correlation, and deterministic execution trace reconstruction, the system requires a **monotonic global retirement clock**:
$$S_{\text{global}} \in [1, 2, \dots, N_{\text{total}}]$$

The `SmpScheduler` manages $S_{\text{global}}$ alongside per-core retirement counters.

---

## 2. Monotonic Global Sequence Rules

1. **Strict Monotonicity**: Every single instruction retired by any active virtual core increments $S_{\text{global}}$ by exactly 1:
   $$S_{\text{global}}(t+1) = S_{\text{global}}(t) + 1$$
2. **Zero Gaps**: The global sequence is strictly dense; no sequence numbers are skipped or wasted.
3. **Total Ordering of Multi-Core Events**: Any event $E_1$ occurring on Core $A$ at $S_1$ and event $E_2$ occurring on Core $B$ at $S_2$ are strictly ordered:
   $$S_1 < S_2 \implies E_1 \text{ strictly precedes } E_2$$
   $$S_1 > S_2 \implies E_2 \text{ strictly precedes } E_1$$
   Since execution is serialized on a single host thread, $S_1 \neq S_2$ is guaranteed for all retired instructions.

---

## 3. Quantum Transition Records

Every execution slice executed by the scheduler is recorded as an immutable `QuantumTransitionRecord`:

```cpp
struct QuantumTransitionRecord {
    std::size_t core_id;                   // Core that executed this slice
    std::uint64_t start_global_sequence;   // S_global at start of quantum
    std::uint64_t end_global_sequence;     // S_global at end of quantum
    std::uint64_t instructions_retired;    // Instructions executed in this slice
    QuantumTruncationReason reason;        // Reason quantum terminated
};
```

### Audit Properties:
- `end_global_sequence - start_global_sequence == instructions_retired`
- For consecutive slices $k$ and $k+1$:
  `record[k+1].start_global_sequence == record[k].end_global_sequence`
- The complete execution history forms a continuous, uninterrupted progression from sequence 0 to sequence $N_{\text{total}}$.

---

## 4. Timeline Reconstruction & Diagnostic Correlation

By combining `SmpScheduler` transition records with per-core diagnostic `.wtr` trace events, tools can reconstruct a single unified timeline across all cores without clock skew or synchronization ambiguity:

```
S_global: 0 --------- 1000 -------- 2000 -------- 2050 -------- 3050 -------- 3100
Core 0:   [Quantum 0: 1000 insts]                 [Quantum 2: 1000 insts]
Core 1:                       [Quantum 1: 1000 insts]                 [HLT: 50 insts]
Core 2:                                           [WFI: 50 insts]
```

This guarantees reproducible causality analysis across memory accesses, exclusive reservations, and cross-core interrupts.
