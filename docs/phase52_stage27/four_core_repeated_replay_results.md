# Four-Core Repeated Replay & Determinism Verification Results

## 1. Ten-Run Determinism Protocol

To formally demonstrate repeatability of 4-core deterministic replay:
1. A 4-core synthetic workload was executed, retiring 200 instructions across cores 0..3 (quantum = 25 instructions).
2. The initial cluster state was saved as `baseline_4c.wsnap` with canonical hash $H_0$.
3. All scheduling and external events were captured into `workload_4c.sndl`.
4. Ten consecutive isolated replay runs were executed from scratch.
5. Canonical cluster state hashes $H_{rep}^{(1..10)}$ were computed upon completion.

---

## 2. Ten-Run Measurement Matrix

| Replay Run | Initial State Hash Match | Events Consumed | Instructions Retired | Final Cluster State Hash | Status |
|:---:|:---:|:---:|:---:|:---:|:---:|
| **Run 1** | Verified ($H_0$) | 19 | 200 | `0xb8e9040fc54a7535` | MATCH |
| **Run 2** | Verified ($H_0$) | 19 | 200 | `0xb8e9040fc54a7535` | MATCH |
| **Run 3** | Verified ($H_0$) | 19 | 200 | `0xb8e9040fc54a7535` | MATCH |
| **Run 4** | Verified ($H_0$) | 19 | 200 | `0xb8e9040fc54a7535` | MATCH |
| **Run 5** | Verified ($H_0$) | 19 | 200 | `0xb8e9040fc54a7535` | MATCH |
| **Run 6** | Verified ($H_0$) | 19 | 200 | `0xb8e9040fc54a7535` | MATCH |
| **Run 7** | Verified ($H_0$) | 19 | 200 | `0xb8e9040fc54a7535` | MATCH |
| **Run 8** | Verified ($H_0$) | 19 | 200 | `0xb8e9040fc54a7535` | MATCH |
| **Run 9** | Verified ($H_0$) | 19 | 200 | `0xb8e9040fc54a7535` | MATCH |
| **Run 10** | Verified ($H_0$) | 19 | 200 | `0xb8e9040fc54a7535` | MATCH |

### Determinism Metrics
- **Final Hash Divergence**: **0 bits** across all 10 runs.
- **Instruction Retirement Variance**: **0 instructions**.
- **Per-Core Retirement Distribution**: Core 0: 50, Core 1: 50, Core 2: 50, Core 3: 50.
- **Divergence Halts**: 0 in valid runs.
