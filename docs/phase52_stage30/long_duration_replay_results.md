# Long-Duration Deterministic Replay Results

## 1. Workload Profiles and Test Executions
Stage 30 verified deterministic replay across four duration profiles spanning four topologies (1, 2, 4, and 8 virtual cores):

| Profile | Total Instructions | Cores | Workload Character | Result | Events Consumed | RSS Peak |
|---|---|---|---|---|---|---|
| L1 | 160,000 | 1, 2, 4, 8 | Arithmetic (MOVZ, ADD, B) | PASS | 3,200 | 8 MB |
| L2 | 1,000,000 | 1, 2, 4, 8 | Tight Branching Loop (ADD, B) | PASS | 10,000 | 8 MB |
| L3 | 5,000,000 | 4, 8 | Shared-Memory Contention (STR, DMB, B) | PASS | 10,000 | 8 MB |
| L4 | 10,000,000 | 8 | Mixed Heterogeneous Workload | PASS | 15,000 | 8 MB |

## 2. Repeated Execution Invariance (10 Runs)
Profile L2 (1,000,000 instructions on 8 cores) was executed 10 consecutive times from a single recorded SNDL v2 and `.wsnap v2` baseline.

- Run 1 Final Hash: `0xba9e8080e9738eca`
- Run 2 Final Hash: `0xba9e8080e9738eca`
- Run 3 Final Hash: `0xba9e8080e9738eca`
- Run 4 Final Hash: `0xba9e8080e9738eca`
- Run 5 Final Hash: `0xba9e8080e9738eca`
- Run 6 Final Hash: `0xba9e8080e9738eca`
- Run 7 Final Hash: `0xba9e8080e9738eca`
- Run 8 Final Hash: `0xba9e8080e9738eca`
- Run 9 Final Hash: `0xba9e8080e9738eca`
- Run 10 Final Hash: `0xba9e8080e9738eca`

Result: 10/10 runs produced identical canonical cluster hash `0xba9e8080e9738eca`. Zero drift or divergence.

## 3. Resource Stability Under Long Execution
- Process RSS at start of L4: 8 MB
- Process RSS at 10,000,000 instructions: 8 MB
- Memory leak detection: None. Buffer reuse in `SndlV2ReplayReader` and `SmpSharedMemory` maintains flat memory allocation.
