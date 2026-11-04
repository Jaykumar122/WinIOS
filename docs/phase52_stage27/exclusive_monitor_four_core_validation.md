# Four-Core Exclusive Monitor Validation

## 1. Exclusive Monitor Subsystem

`SmpExclusiveMonitorManager` tracks exclusive load and store operations across cores in accordance with ARMv8-A architecture:
- **Granule Size**: 64-byte aligned cache lines.
- **`LDXR` / `LDAXR`**: Registers an active reservation for the calling core at the target 64-byte granule.
- **`STXR` / `STLXR`**: Checks whether calling core's reservation is valid:
  - If valid: commits the store, invalidates all reservations overlapping that 64-byte granule across all other cores, clears caller reservation, and returns status 0 (success).
  - If invalid: does not commit store, returns status 1 (failure).
- **`CLREX`**: Explicitly clears reservation for the calling core.

---

## 2. Four-Core Contention Workload

In `test_group_8_exclusive_monitor_contention`:
1. Core 0 sets reservation at `0x30000`.
2. Core 1 sets reservation at `0x30000` (overlapping 64-byte granule).
3. Core 0 executes successful `STXR` to `0x30000`.
4. Core 1's reservation is automatically cleared (`ReservationCleared`).
5. Core 1's subsequent `STXR` fails deterministically.

### Verification Results
- All reservation sets, clears, and store exclusive results replay identically across 10 runs.
- Final cluster state hash matches recorded state exactly.
