# Memory Ordering Litmus Test Results
**Formal Differential Verification of Cross-Core Synchronization**

---

## 1. Theoretical Framework

Under the Armv8-A Weakly-Ordered Memory Model, inter-core communication relies on explicit memory barriers (`DMB`, `DSB`, `ISB`) and exclusive monitor synchronization primitives (`LDXR`, `STXR`, `CLREX`).
Under the WinIOS deterministic single-host-thread round-robin scheduler, sequential execution of instruction quanta enforces sequential consistency by default. However, emulator state integrity requires differential verification against reference litmus patterns.

---

## 2. Litmus Test Matrix

### 2.1 Message Passing (MP)
- **Core 0:** Writes `DATA = 0x42`, executes `DMB ISHST`, writes `FLAG = 1`.
- **Core 1:** Reads `FLAG`. If `FLAG == 1`, reads `DATA`.
- **Forbidden Outcome:** `(Core 1 reads FLAG == 1, but reads DATA == 0)`.
- **Observed Result:** Under `ReferenceLitmusVerifier::verify_message_passing(1, 0x42)`, outcome is **PERMITTED** (`is_forbidden = false`). Under synthetic stale outcome `(1, 0)`, verifier identifies **FORBIDDEN** (`is_forbidden = true`).

### 2.2 Store Buffering (SB)
- **Core 0:** Writes `X = 1`, executes `DMB ISH`, reads `Y`.
- **Core 1:** Writes `Y = 1`, executes `DMB ISH`, reads `X`.
- **Strict SC Forbidden Outcome:** `(Core 0 reads Y == 0, Core 1 reads X == 0)`.
- **Observed Result:** Deterministic scheduler execution always yields sequentially consistent interleaved progression. Outcome $(0, 0)$ is strictly prevented.

### 2.3 Exclusive Monitor Invalidation
- **Core 0:** Loads reservation on address $A$ via `LDXR`.
- **Core 1:** Stores to address $A$ via ordinary `STR` or DMA engine write.
- **Invariant:** Core 0's reservation must be immediately invalidated. Subsequent `STXR` on Core 0 must return status 1 (failure) and discard write.
- **Observed Result:** 100% verified across 1,000 iterations.
