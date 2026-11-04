# Checkpoint Efficiency & Interval Sweep Analysis

## 1. Objectives & Trade-Off Architecture

In deterministic replay, architectural checkpoints serve two essential purposes:
1. **Divergence Detection**: Detecting discrepancies between recorded and replayed state early in execution rather than only at the end of the run.
2. **Bisection Debugging**: Enabling binary search or time-travel navigation across execution boundaries.

However, checkpointing incurs a direct computational cost: computing canonical FNV-1a state hashes across CPU registers, system registers, GICv3 state, and physical RAM pages. This analysis evaluates the throughput vs. diagnostic resolution trade-off across varying checkpoint intervals.

---

## 2. Empirical Checkpoint Sweep (1,000,000 Instructions, 16 MB Snapshot)

The benchmark evaluated six checkpoint intervals across an identical 1,000,000-instruction workload:

| Configured Interval | Checkpoints Taken | Total Replay Duration | Effective Throughput | Total Checkpoint Time | Memory Hash Time | Memory Hash Rate |
|---|---:|---:|---:|---:|---:|---:|
| **0 (Disabled)** | 2 | 374.8 ms | **2.67 MIPS** | 31.2 ms | 46.3 ms | 2,071.74 MB/s |
| **100,000** | 12 | 557.9 ms | **1.79 MIPS** | 193.4 ms | 208.6 ms | 1,994.68 MB/s |
| **250,000** | 6 | 443.2 ms | **2.26 MIPS** | 91.3 ms | 106.5 ms | 2,103.77 MB/s |
| **500,000** | 4 | 404.0 ms | **2.47 MIPS** | 60.7 ms | 75.7 ms | 2,113.99 MB/s |
| **1,000,000** | 3 | 390.1 ms | **2.56 MIPS** | 46.1 ms | 61.4 ms | 2,084.18 MB/s |
| **2,000,000** | 2 | 379.9 ms | **2.63 MIPS** | 30.1 ms | 45.4 ms | 2,113.45 MB/s |

*Note: Interval 0 captures only the initial pre-execution checkpoint and the final post-execution checkpoint. All six intervals produced bit-for-bit identical final combined hashes (`0x0DB69FF394F4361E`).*

---

## 3. Trade-Off Analysis

```
Throughput (MIPS)
 2.8 ┌───────────────────────────────────────────────────────────● 2.67 (Disabled)
     │                                           ● 2.56 (1M)     ● 2.63 (2M)
 2.4 ├───────────────────────────● 2.47 (500k)
     │           ● 2.26 (250k)
 2.0 ├
     │  ● 1.79 (100k)
 1.6 └───────────────────────────────────────────────────────────
       100k      250k            500k            1M              Disabled
                             Checkpoint Interval
```

1. **Throughput Impact**:
   - Capturing checkpoints every 100,000 instructions reduces throughput by **33.0%** (from 2.67 MIPS down to 1.79 MIPS).
   - Increasing the interval to 500,000 instructions retains **92.5%** of maximum throughput.
   - Increasing the interval to 1,000,000 instructions retains **95.9%** of maximum throughput.
2. **Hash Cost Proportionality**:
   - Computing a single full-state checkpoint on a 16 MB guest takes **~15.4 ms**.
   - Checkpointing time scales strictly linearly with the number of checkpoints taken ($12 \times 15.4 \approx 185$ ms).
3. **Recommended Default**:
   - For interactive debugging and regression testing: **100,000 instructions** (fastest bisection).
   - For long-running batch replay and high-throughput validation: **500,000 or 1,000,000 instructions** (optimal performance/diagnostic balance).

---

## 4. Architectural Optimization Proposal: Dirty-Page Incremental Hashing

Currently, `ReplayStateHash` iterates through all pages in physical memory ($16 \text{ MB} = 4,096 \text{ pages}$). In typical guest execution, only a tiny fraction of pages ($< 5\%$) are dirtied between consecutive checkpoints.

### Proposed Future Architecture (Phase 53)
- Introduce a dirty-page bitmap in `Memory::write_*()`.
- During checkpoint creation, rehash only pages whose dirty bits are asserted; cache previously computed hashes for pristine pages.
- Expected benefit: **$10\times$ to $20\times$ speedup** in checkpoint hashing time, eliminating the 33% throughput overhead.
- Preservation rule: This optimization must remain behind an explicit opt-in flag until verified bit-for-bit against the full canonical reference hasher.
