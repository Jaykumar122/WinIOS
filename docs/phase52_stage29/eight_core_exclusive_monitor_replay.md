# Eight-Core Exclusive Monitor Contention Replay

## 1. Exclusive Monitor Tracking
The `SmpExclusiveMonitorManager` tracks exclusive reservations across 8 cores:
- Load-exclusive instructions (`LDXR`, `LDAXR`) register physical address reservations via `ReservationSet`.
- Store-exclusive instructions (`STXR`, `STLXR`) evaluate reservation status:
  - If valid and address matches, the store succeeds (status 0), clears reservations on conflicting cores via `ReservationCleared`, and emits `StoreExclusiveResult` with success = 1.
  - If invalid or address was invalidated by another core, the store fails (status 1) and emits `StoreExclusiveResult` with success = 0.

## 2. Replay Determinism under Contention
Because lock acquisition order depends critically on quantum boundaries and core scheduling order:
1. All 8 cores contending for spinlocks or ticket locks record every reservation and store-exclusive outcome.
2. During replay, `StoreExclusiveResult` ensures that each core's exclusive operations yield the identical success/failure status.
3. This eliminates race conditions during lock replay and ensures 100% reproducible synchronization.
