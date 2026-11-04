# Shared Memory and Exclusive Monitor Event Capture

## 1. Single-Host-Thread Determinism Analysis
Under the WinIOS Stage 22/24 SMP model:
- All virtual cores execute on a single host thread via `SmpScheduler`.
- Shared physical memory accesses (loads and stores) occur in a strictly serialized, deterministic sequence defined by round-robin scheduling and instruction quantums $Q$.
- Ordinary shared-memory loads and stores are therefore **100% deterministic** and do not require individual per-access logging. Blindly recording loads and stores would create gigabytes of redundant trace data.

---

## 2. Nondeterministic & Synchronization Boundary Events
Certain operations represent explicit architectural synchronization points or may involve nondeterministic external invalidations (e.g., DMA or external processor writes). SNDL v2 defines structured records for these boundaries:

| Event Type | Value | Boundary | Description |
|---|---|---|---|
| `MemoryBarrierSync` | 60 | `BarrierBoundary` | Memory barrier instruction (DMB, DSB, ISB): `payload1 = barrier_type`. |
| `ReservationSet` | 61 | `DuringInstruction` | Load-exclusive (`LDXR`) established reservation: `payload1 = paddr`. |
| `ReservationCleared` | 62 | `DuringInstruction` | Clear-exclusive (`CLREX`) or cross-core invalidation cleared reservation: `payload1 = paddr`. |
| `StoreExclusiveResult` | 63 | `DuringInstruction` | Store-exclusive (`STXR`) outcome: `payload1 = paddr`, `payload2 = 1 (success) / 0 (failure)`. |

---

## 3. Strict Scope & Architectural Limits
Stage 24 records reservation and outcome events for exclusive monitor operations without altering the underlying `SmpExclusiveMonitorManager` state machine.
This does NOT claim complete multi-threaded ARM weak memory model emulation; it captures outcomes under the single-host-thread deterministic scheduler.
