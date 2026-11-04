# WinIOS Phase 52 Stage 26 — Retirement Synchronization & Faulting Invariants

## 1. Dual-Clock Retirement Architecture

Multi-core deterministic replay requires continuous alignment of two related retirement counters:
1. **Global Retirement Sequence ($R_{global}$):** The cluster-wide monotonic instruction retirement sequence. Each architecturally retired instruction on any core advances $R_{global}$ by 1.
2. **Per-Core Retirement Count ($R_{core}(c)$):** The private instruction retirement counter of core $c$.

At any point in cluster execution, the following fundamental conservation invariant must hold:
$$R_{global} = \sum_{c=0}^{N-1} R_{core}(c)$$

```
                      Global Retirement Clock (R_global)
                   +--------------------------------------+
                   | 0  1  2  3  4  5  6  7  8  9  10 ... |
                   +--------------------------------------+
                             /                  \
                            /                    \
                           v                      v
                Core 0 Retirement (R_0)   Core 1 Retirement (R_1)
                +---------------------+   +---------------------+
                | 0  1  2  3  4  5    |   | 0  1  2  3  4  5    |
                +---------------------+   +---------------------+
```

---

## 2. Invariants and Guardrails

### 2.1 Faulting Instructions Do Not Retire
In AArch64 architecture, instructions that raise synchronous exceptions (e.g. data aborts, undefined instructions, SVC, BRK) branch to the exception vector rather than completing architectural retirement.
- `CPU::step()` sets `exception_taken_in_step(true)`.
- The retirement counters $R_{global}$ and $R_{core}(c)$ are **not** incremented.
- The coordinator consumes the `ExceptionTaken` SNDL v2 event without advancing retirement counters.
- This invariant prevents retirement divergence between recording and replay.

### 2.2 Continuous Boundary Verification
Rather than deferring retirement checks until the end of the entire recording, the replay coordinator verifies retirement at every quantum boundary:
- When consuming `QuantumEnd`, `rec.payload1` specifies the exact number of instructions retired in that quantum slice.
- Coordinator verifies `rec.payload1 == slice_executed`.
- If a guest core retired fewer or more instructions than recorded, a `RetirementMismatch` divergence is raised immediately.

### 2.3 Post-Replay Final Conservation Check
At replay completion, coordinator verifies:
1. `scheduler->global_retirement_sequence() == cursor.footer().last_global_retirement_sequence`.
2. $\sum R_{core}(c) == \text{stats.total\_instructions\_retired}$.
3. Zero regression: counters are strictly monotonic throughout execution.
