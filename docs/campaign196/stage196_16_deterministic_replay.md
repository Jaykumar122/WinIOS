# Stage 196.16 — Deterministic SMP Replay & Failure Reproduction (Acceptance Test N)

## 1. Architectural Implementation
- **Deterministic SMP Cluster Execution:**
  - `SMPCluster::run_deterministic` steps active cores in deterministic round-robin order.
  - Multi-core atomic interactions, load/store exclusive evaluations, and lock transitions execute in a repeatable instruction sequence.
- **Canonical State Hashing Engine (`ReplayStateHash`):**
  - Component-level architectural state hashing covering:
    - GPR registers (X0..X30, PC, SP_EL0, SP_EL1)
    - SIMD registers (Q0..Q31)
    - System registers (SPSR_EL1, ELR_EL1, ESR_EL1, FAR_EL1, TTBR0_EL1, TPIDR_EL0, TPIDR_EL1)
    - Guest memory footprint (`hash_memory`)
    - Combined canonical 64-bit hash (`combined_hash`)
  - Divergence detection via `compare_and_diff`, identifying exact architectural mismatch points when execution paths diverge.

## 2. Test Evidence (Acceptance Test N)
Verified via `stage196_16_deterministic_replay_tests`:
- Test N1: Exact bit-identical replay across repeated runs of 4-core contended atomic `LDADD` workload (604 total retired instructions).
  - Core 0 combined hash: `0xab1cdd2a76b453ff`
  - Core 1 combined hash: `0x6af69e56f8813db6`
  - Core 2 combined hash: `0x13c82278dac9b2a3`
  - Core 3 combined hash: `0x26489024759e4bef`
  - Final counter: `200` (4 cores * 50 increments, zero lost updates).
- Test N2: Divergence injection detected by `compare_and_diff` with detailed human-readable diff reporting.
- Results recorded in `docs/campaign196/replay_results.json`.

## 3. Evidence Classification
`GUEST_SMP`, `GUEST_ATOMIC`, `SYNTHETIC_KERNEL`. Tested in Debug and Release.
