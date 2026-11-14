# Stage 196.6 — Acquire/Release Memory Ordering (Acceptance Test C)

## 1. Architectural Litmus Tests
ARMv8-A specifies that acquire loads (`LDAR`, `LDAXR`) and release stores (`STLR`, `STLXR`) create a one-way barrier enforcing ordering:
- **Message Passing (MP):**
  - Thread 1 stores payload data via ordinary store, then sets flag via `STLR`.
  - Thread 2 reads flag via `LDAR`, then reads payload data.
  - Architectural Invariant: If Thread 2 observes `flag == 1`, it must observe payload `data == 42`. The outcome `flag == 1 && data == 0` is forbidden.
- **Store Buffering (SB):**
  - Thread 1 writes X=1, executes `DMB SY`, reads Y.
  - Thread 2 writes Y=1, executes `DMB SY`, reads X.
  - Architectural Invariant: With full memory barrier `DMB SY`, both threads reading 0 is architecturally forbidden.
- **Lock Acquisition Visibility:**
  - Producer writes multiple payload fields inside critical section, releases lock with `STLR`.
  - Consumer acquires lock with `LDAXR`/`STLXR`, reads payload.
  - Architectural Invariant: All updates performed before the release store are guaranteed visible after the acquire load.

## 2. Test Execution Details
Implemented and executed via `stage196_6_acquire_release_tests`:
- Test C1: MP litmus test (Release/Acquire ordering).
- Test C2: SB litmus test (DMB SY barrier).
- Test C3: Lock-based publication and data visibility.

## 3. Evidence Classification
`GUEST_MEMORY_ORDER`, `GUEST_SMP`.
Verified with real ARM64 instruction execution across multi-core `SMPCluster`.
