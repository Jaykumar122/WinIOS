# Independent Expected-State Validation

## 1. Methodology
To prevent circular self-validation (comparing replay state solely to recording state), an independent mathematical ground-truth model was implemented.

The guest workload executes a calibrated 4-instruction loop per core:
1. `ADD Xc, Xc, #(c + 1)`
2. `MOVZ X10, #data_offset`
3. `STR Xc, [X10]`
4. `B .-12`

With 8 cores running exactly 100 instructions each (total 800 instructions):
- Number of complete loop iterations per core: $\frac{100}{4} = 25$.
- Analytical expected value for register $Xc$: $Xc = 25 \times (c + 1)$.
- Analytical expected value at physical memory address $0x4000 + 64 \times c$: $25 \times (c + 1)$.

## 2. Empirical Verification
After full replay completion, state was checked against the independent mathematical equations:

| Core | Retired Instructions | Expected $Xc$ | Actual $Xc$ | Shared RAM Address | Expected RAM Value | Actual RAM Value |
|---|---|---|---|---|---|---|
| Core 0 | 100 | 25 | 25 | 0x4000 | 25 | 25 |
| Core 1 | 100 | 50 | 50 | 0x4040 | 50 | 50 |
| Core 2 | 100 | 75 | 75 | 0x4080 | 75 | 75 |
| Core 3 | 100 | 100 | 100 | 0x40C0 | 100 | 100 |
| Core 4 | 100 | 125 | 125 | 0x4100 | 125 | 125 |
| Core 5 | 100 | 150 | 150 | 0x4140 | 150 | 150 |
| Core 6 | 100 | 175 | 175 | 0x4180 | 175 | 175 |
| Core 7 | 100 | 200 | 200 | 0x41C0 | 200 | 200 |

## 3. Structural Invariants
- Total cluster instructions retired: 800.
- Per-core retirement distribution: [100, 100, 100, 100, 100, 100, 100, 100].
- Core PCs: All cores returned to $0x2000 + 0x200 \times c$ exactly.
- Core Lifecycle: All 8 cores retained `CoreLifecycleState::Runnable`.
