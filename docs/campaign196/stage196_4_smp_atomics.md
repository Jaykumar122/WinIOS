# Stage 196.4 — Atomicity Under SMP Execution (Acceptance Test B)

## 1. Multi-Core SMP Concurrency Invariants
- **No Lost Updates:** Under concurrent execution from multiple virtual cores, every atomic increment or swap must be serialized and committed without losing updates.
- **Contention Resilience:**
  - `LDXR` / `STXR`: If another core writes to the shared counter (in the same 64-byte granule) between `LDXR` and `STXR`, `STXR` returns status 1 (failure) and leaves memory untouched. The guest retry loop repeats until successful reservation commit.
  - `CAS`: If another core updates memory before `CAS` commits, `CAS` loads the latest memory value into the expected register and aborts the store. The guest retry loop branches back and retries with the new expected value.
  - `LDADD`: Single-instruction atomic fetch-and-add is serialized across the cluster.

## 2. Test Execution Details
Tested via `stage196_4_smp_atomics_tests`:
1. **4-Core Contended LDADD:**
   - 4 virtual cores, 100 iterations per core.
   - Shared memory counter initialized to 0.
   - Expected final counter: exactly 400. Zero lost updates.
2. **4-Core Contended LDXR/STXR:**
   - 4 virtual cores, 50 iterations per core with reservation collision retry.
   - Expected final counter: exactly 200. Failed exclusive stores retried correctly.
3. **4-Core Contended CAS:**
   - 4 virtual cores, 50 iterations per core with compare-and-swap retry loop.
   - Expected final counter: exactly 200. Zero lost or duplicated updates.

## 3. Evidence Classification
`GUEST_ATOMIC`, `GUEST_SMP`.
Verified with real ARM64 instruction execution across multi-core `SMPCluster`.
