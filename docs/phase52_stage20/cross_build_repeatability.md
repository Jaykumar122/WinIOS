# Cross-Build & Multi-Run Repeatability Acceptance

## 1. Acceptance Criteria: Bit-for-Bit Determinism

The core requirement of Phase 52 Stage 20 is deterministic repeatability. Determinism is proven when:
1. **Multi-Run Invariance**: Executing the exact same snapshot and SNDL stream ten consecutive times in the same process produces bit-for-bit identical final architectural state hashes.
2. **Cross-Speed Invariance**: Executing an instruction sequence at full speed, instruction-by-instruction via single stepping, or pausing at every instruction boundary produces identical intermediate and final states.
3. **Cross-Build Invariance**: Binary execution compiled under Debug (`/Od`, assertions enabled, unoptimized frame pointers) produces the identical architectural state hashes as Release (`/O2`, `/Oi`, `/Gy`, compiler auto-vectorization, inlined loops).

---

## 2. Experimental Methodology

To evaluate repeatability, a mixed test program combining integer arithmetic (`MOVZ`, `MOVK`, `ADD`), memory store/load operations (`STR`, `LDR`), and synchronous SNDL event injection was executed across multiple configurations.

```
       Initial Snapshot (.wsnap) + Input Log (.sndl)
                             │
       ┌─────────────────────┼─────────────────────┐
       ▼                     ▼                     ▼
[Run 1: Batch Replay]  [Run 2: Step-by-Step]  [Runs 3–10: Repeated]
       │                     │                     │
       ▼                     ▼                     ▼
Final Hash = H1       Final Hash = H2       Final Hashes = H3..H10
       │                     │                     │
       └─────────────────────┼─────────────────────┘
                             ▼
                Verify: H1 == H2 == H3 == ... == H10
```

---

## 3. Results & Empirical Verification (Test Group H)

### 10 Consecutive Repeated Replays
Ten independent replay sessions were created, prepared, executed, and torn down in a single test process:
- Run 1 Final Hash:  `0x9115049FFCCFE4EE`
- Run 2 Final Hash:  `0x9115049FFCCFE4EE`
- Run 3 Final Hash:  `0x9115049FFCCFE4EE`
- Run 4 Final Hash:  `0x9115049FFCCFE4EE`
- Run 5 Final Hash:  `0x9115049FFCCFE4EE`
- Run 6 Final Hash:  `0x9115049FFCCFE4EE`
- Run 7 Final Hash:  `0x9115049FFCCFE4EE`
- Run 8 Final Hash:  `0x9115049FFCCFE4EE`
- Run 9 Final Hash:  `0x9115049FFCCFE4EE`
- Run 10 Final Hash: `0x9115049FFCCFE4EE`

**Result**: 100% bit-for-bit identical. Variance = 0.

### Step-by-Step vs Continuous Execution
- Continuous batch run hash: `0x9115049FFCCFE4EE`
- Single-stepped instruction run hash: `0x9115049FFCCFE4EE`
- Every-instruction checkpointed run hash: `0x9115049FFCCFE4EE`

**Result**: Stepping granularity has zero influence on the architectural outcome.

### Cross-Build Verification (Debug vs Release)
- Debug build (`replay_hardening_tests.exe`): All 11 test groups PASSED. Hash = `0x9115049FFCCFE4EE`.
- Release build (`replay_hardening_tests.exe`): All 11 test groups PASSED. Hash = `0x9115049FFCCFE4EE`.

**Result**: Compiler optimization level has zero effect on guest architectural state.
