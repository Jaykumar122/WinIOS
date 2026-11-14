# Stage 196.7 — Kernel Spinlock Correctness (Acceptance Test D)

## 1. Architectural Implementation
- **Data Layout:** `GuestSpinlockData` (16 bytes packed):
  - `lock` (uint32_t): 0 = unlocked, 1 = locked.
  - `owner_tid` (uint32_t): Thread ID of lock holder.
  - `lock_count` (uint32_t): Recursion / contention metrics.
  - `flags` (uint32_t): Attribute flags.
- **Mutual Exclusion & Recursion:**
  - `try_acquire_host`: Atomically verifies lock is 0, checks if current caller is already holder (recursive acquisition flagged as deadlock via `out_deadlock = true`), assigns owner TID.
  - `release_host`: Verifies caller is actual owner; releases lock and clears owner TID.
- **Guest Machine Code Emitters:**
  - `emit_guest_spinlock_acquire`: Emits `LDAXR`, `CBNZ`, `MOV`, `STLXR`, `STR owner`, `RET`.
  - `emit_guest_spinlock_release`: Emits `STR WZR, [X0, #4]`, `STLR WZR, [X0]`, `RET`.

## 2. Evidence Classification
`SYNTHETIC_KERNEL`, `GUEST_ATOMIC`.
Verified via `stage196_7_spinlock_stress_tests`.
