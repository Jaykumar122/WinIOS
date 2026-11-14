# Stage 196.2 — Exclusive Monitor Correctness (Acceptance Test A)

## 1. Architectural Implementation
- **Cache-line Reservation Granule:** Aligned to 64 bytes (`kGranuleSize = 64`, `kGranuleMask = ~63`), matching Apple A16 Bionic architectural cache lines.
- **Cross-Core Invalidation:** Writes from another virtual core or DMA within the 64-byte granule invalidate the reservation in `SMPCluster` and `SmpExclusiveMonitorManager`.
- **Instruction Validation:**
  - `LDXR` / `LDAXR`: Establishes core-private and cluster-level reservation tags.
  - `STXR` / `STLXR`: Verifies address, size, and granule validity. On success, writes memory, returns status 0 in the destination register, and consumes the reservation. On failure, aborts store, returns status 1.
  - `CLREX`: Explicitly clears reservation for the executing PE.
  - Exception and `ERET`: Entering an exception handler or executing `ERET` unconditionally clears the local reservation, preventing cross-context reservation leaks.

## 2. Test Evidence (Acceptance Test A)
Executed via `stage196_2_exclusive_monitor_tests`:
- Test A1: Granule reservation establishment & cross-granule independence.
- Test A2: `LDXR` / `STXR` success (status 0) and failure on repeated store without reload (status 1).
- Test A3: Explicit `CLREX` clears reservation; subsequent store-exclusive returns status 1.
- Test A4: Cross-core store to another address within the same 64-byte granule invalidates reservation; subsequent store-exclusive returns status 1.
- Test A5: `ERET` clears reservation; subsequent store-exclusive returns status 1.

All tests passed in Debug and Release with zero compiler warnings under `/W4 /WX /permissive-`.
Evidence Classification: `GUEST_ATOMIC`, `GUEST_SMP`.
