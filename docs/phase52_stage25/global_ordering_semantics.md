# WinIOS — Phase 52, Stage 25: Global Ordering & Dual Sequence Semantics

## 1. Ordering Specification
In multi-core virtual execution, deterministic replay requires a total ordering of all non-deterministic inputs and inter-core scheduling boundaries. SNDL v2 defines a dual-counter sequencing model:

1. **Global Event Sequence ($S$)**:
   - Strictly monotonic ($S_{i+1} = S_i + 1$).
   - Assigned at event observation during scheduler quantum transitions or input submission.
   - Provides an unambiguous, gap-free linear timeline of every captured event across all virtual cores.
2. **Global Retirement Sequence ($R_{global}$)**:
   - Monotonically non-decreasing cluster instruction counter ($R_{global, i+1} \ge R_{global, i}$).
   - Tracks the exact total number of architectural instructions completed across all cores up to that point.
3. **Core Retirement Sequence ($R_{core}(c)$)**:
   - Monotonically non-decreasing per-core instruction counter.
   - Enables positioning the exact instruction retirement boundary within a core's local thread of execution.

## 2. Sequencing Invariants Enforced by Validator
- $S_{i+1} = S_i + 1$ (Consecutive global sequence in strict mode; $S_{i+1} > S_i$ in standard mode).
- $R_{global, i+1} \ge R_{global, i}$ (Cluster retirement never goes backward).
- $R_{core}(c)_{i+1} \ge R_{core}(c)_i$ (Per-core retirement never goes backward).
- $\text{core\_id} < \text{header.core\_count}$ (or `0xFFFFFFFF` for global events).
- In a valid completed file, `footer.last_global_sequence` equals the final record's global sequence.
