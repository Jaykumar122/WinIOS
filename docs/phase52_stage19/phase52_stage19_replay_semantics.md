# WinIOS Phase 52, Stage 19 — Deterministic Replay Semantics

## 1. Instruction Retirement Boundaries

In deterministic replay, execution progress is governed by discrete, monotonic instruction retirement boundaries rather than wall-clock time:

1. **Retirement Count (`instruction_retirement_count_`)**:
   - Every architecturally retired AArch64 instruction increments `instruction_retirement_count_` by exactly 1.
   - Instructions that trigger synchronous exceptions before completion (e.g. data aborts, undefined instructions) do not retire and do not advance the retirement count.
   - Stepping occurs strictly sequentially on the primary core.

2. **Temporal Quantization**:
   - The execution timeline is completely defined by integer retirement counts: $R \in \mathbb{N}_0$.
   - Any external input or asynchronous event must be precisely correlated to the retirement count $R$ at which it took effect during recording.

---

## 2. Event Boundaries and Ordering Invariants

During execution recording (Stage 18), events were classified into five distinct boundary types:

| Boundary Type | SndlBoundaryType | Point of Occurrence | Injection / Interception Semantics |
|---|---|---|---|
| **BeforeInstruction** | 1 | Prior to instruction fetch/decode | Synchronized with $R$ prior to stepping |
| **DuringInstruction** | 2 | Inside instruction execution | Intercepted synchronously via inline hooks |
| **AfterInstruction** | 3 | Immediately upon retirement | Verified immediately after $R$ increments |
| **BetweenInstructions** | 4 | In quiescent pipeline state | Processed before next fetch |
| **ExceptionBoundary** | 6 | Exception/Interrupt vector entry | Dispatched to vector entry before instruction step |

### 2.1 Sub-Instruction Ordinal Invariants

Certain complex instructions perform multiple non-deterministic interactions within a single architectural retirement boundary (e.g., paired loads `LDP` reading from MMIO, or multiple counter accesses):

- Each event occurring within the same `(core_id, instruction_retirement_count)` tuple is assigned a zero-indexed `sub_instruction_ordinal`:
  $$O \in \{0, 1, 2, \dots\}$$
- Invariants:
  1. For events within the same retirement count, ordinals must strictly monotonically increase: $O_{k+1} > O_k$.
  2. The input router verifies both $R$ and $O$. If an MMIO read occurs at $O=1$ when the log expects $O=0$, an `EventSequenceMismatch` or `BoundaryMismatch` is recorded.

---

## 3. Asynchronous Input Injection Semantics

Asynchronous inputs include external interrupts (GIC SPIs/PPIs) and hardware timer expirations:

1. **Pre-Step Delivery**:
   - Prior to stepping the CPU at instruction retirement count $R$, the scheduler queries `cursor.has_pending_asynchronous_input(core_id, R)`.
   - If an event is pending at retirement count $R$, the scheduler consumes the record from the SNDL cursor and invokes the CPU exception architecture:
     - IRQ / FIQ delivery calls `raise_asynchronous_exception()` or `raise_el2_asynchronous_exception()` / `raise_el3_asynchronous_exception()` matching the recorded `target_el`.
   - This switches PSTATE, saves ELR/SPSR, vectors to the appropriate VBAR offset, and transitions execution state *identically* to the recorded session.
2. **Deterministic Unwinding of Wait States**:
   - If the CPU is in a `WFI` (Wait For Interrupt) state, the arrival of a recorded asynchronous interrupt wakes the core and causes immediate exception vector entry, exactly matching recording.

---

## 4. Synchronous Query Interception Semantics

For synchronous operations, the live interpreter does not execute live hardware interactions:

1. **MMIO Reads (`hook_replay_mmio_read`)**:
   - Invoked directly from `MemoryAccessor::read()` when `cpu_.get_gic().is_mmio_address(pa)` is true.
   - If `is_replay_active()` is true, the router validates:
     - `core_id == expected_core_id`
     - `pa == expected_paddr`
     - `width == expected_width`
   - If valid, returns the recorded `rec.payload2` and advances the cursor without querying the device registers.
   - If invalid, records an `MMIOAddressMismatch` or `MMIOWidthMismatch` divergence.

2. **Counter Reads (`hook_replay_counter_read`)**:
   - Invoked on `CNTPCT_EL0` and `CNTVCT_EL0` reads.
   - Returns recorded counter timestamp directly, decoupling replay execution speed from host CPU frequency or time of day.

3. **True Random / Entropy (`hook_replay_entropy`)**:
   - Invoked on RNDR/RNDRRS system register access.
   - Supplies the recorded pseudo-random word, ensuring randomized guest algorithms (e.g. ASLR, cryptographic key generation) follow the exact recorded trace.
