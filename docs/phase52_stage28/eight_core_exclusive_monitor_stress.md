# Eight-Core Exclusive Monitor Stress
## Exclusive Monitor Subsystem Contention & Invalidation Testing Across Cores 0..7

---

## 1. Monitor Subsystem Design

`SmpExclusiveMonitorManager` provides ARMv8-A architectural exclusive monitor tracking:
- **Reservation Granule**: 64 bytes (`0x40`), aligned to `address & ~0x3FULL`.
- **Per-Core Local Monitors**: Core $i$ enters exclusive state upon `LDXR` at granule $G$.
- **Global Invalidation**: A store operation (`STXR`, `STR`) to granule $G$ by any core clears reservations for all other cores holding a reservation on $G$.

---

## 2. Eight-Core Exclusive Monitor Stress Workload (Workload 3)

- **Setup**: All 8 virtual cores register exclusive reservations on a shared cache line at address `0x4000`.
- **Contention Sequence**:
  1. Cores 0..7 execute `set_reservation(core_id, 0x4000)`. All 8 cores hold local reservations.
  2. Core 3 successfully completes an atomic update and calls `clear_reservations_except(3, 0x4000)`.
  3. Validation confirms:
     - Core 3 reservation state matches expected transition.
     - Cores 0, 1, 2, 4, 5, 6, 7 have their reservations invalidated immediately (`check_reservation(...) == false`).
- **Result**: PASSED. Zero false-positive reservations, strict invalidation enforcement across 8 cores.
