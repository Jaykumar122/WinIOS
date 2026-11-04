# WinIOS Phase 52 Stage 18: Event Ordering & Boundary Semantics

## 1. Overview

Deterministic replay requires that every nondeterministic input recorded during original execution is consumed at the **exact same logical execution point** during replay. 

In multi-core SMP virtual machines, real wall-clock time and physical thread execution speeds are non-repeatable. Therefore, SNDL establishes a strict, dual-dimensional logical ordering model based on:
1. **Global Monotonic Sequence Numbers** (`sequence_num`): Defines the total linear causal order across all cores and asynchronous sources.
2. **Per-Core Instruction Retirement Coordinates** (`instruction_retirement_count` + `sub_instruction_ordinal`): Pins each event to the exact cycle of guest execution.

---

## 2. Global Sequence Monotonicity

Every event recorded via `SndlRecorder::submit_record()` is assigned an atomic global sequence number:

$$\text{sequence\_num}_{k+1} = \text{sequence\_num}_k + 1$$

- Global sequence numbers start strictly at `1` for the first record in an SNDL recording.
- Sequence numbers are assigned atomically under lock across all SMP cores and external device threads.
- During file inspection and validation (`SndlValidation`), any sequence number gap, duplication, or backward step ($S_{k+1} \le S_k$) immediately flags the file as corrupted (`SndlErrorCode::NonMonotonicSequence`).

---

## 3. Per-Core Retirement & Sub-Instruction Ordinal

### 3.1 Instruction Retirement Definition
In WinIOS, a guest instruction is defined as **retired** when it has successfully executed to completion without taking an unhandled trap or architectural abort.
- Managed in `src/cpu/cpu.cpp` by `StepCounterGuard::~StepCounterGuard()`:
```cpp
if (retired && !cpu.exception_taken_in_step()) {
    ++cpu.instruction_retirement_count_;
}
```
- Reset to 0 on `CPU::reset()`.
- Synchronous exceptions and traps that abort an instruction before completion do *not* increment the retirement counter.

### 3.2 Sub-Instruction Ordinal (Tie-Breaker)
Certain architectural instructions perform multiple nondeterministic operations within a single instruction retirement. Examples include:
- A 64-bit unaligned or pair memory read (`LDP`) split into two distinct MMIO read transactions.
- Consecutive status register polling within the same instruction step.
- An interrupt injected immediately before a counter read during the same cycle.

To prevent ambiguity, `SndlRecorder` tracks per-core instruction retirement coordinates:
```cpp
struct PerCoreTracking {
    std::uint64_t last_ret_count{0};
    std::uint16_t next_sub_ordinal{0};
};
```
- When an event occurs at the same `instruction_retirement_count` as the preceding event on the same core:
  $$\text{sub\_instruction\_ordinal} = \text{next\_sub\_ordinal}++$$
- When the core's `instruction_retirement_count` advances, `sub_instruction_ordinal` resets to `0`.

---

## 4. Execution Boundary Semantics

Each record explicitly indicates the exact architectural boundary where the input value was sampled or injected via `SndlBoundaryType`:

| Boundary Type | Code | Semantic Definition | Examples |
|---|---|---|---|
| `BeforeInstruction` | 0 | Prior to fetching or decoding the instruction | Trap interception, pre-step breakpoint |
| `DuringInstruction` | 1 | Inside instruction execution phase | MMIO read result, counter read (`CNTPCT_EL0`) |
| `AfterInstruction` | 2 | Immediately following instruction retirement | Post-instruction trace, step counter advance |
| `BetweenInstructions` | 3 | At instruction boundary between instructions | Host input buffer inject, DMA transfer |
| `AsynchronousInput` | 4 | External asynchronous hardware event | SPI interrupt injection, external timer firing |
| `ExceptionBoundary` | 5 | Exception vector routing boundary | Accepted IRQ/FIQ delivery, vector dispatch |

### 4.1 Replay State Machine Matching
During future replay, the execution engine steps instructions until the core's retirement count matches the next record's `instruction_retirement_count`.
- **`DuringInstruction`**: The replayer steps the instruction; when the instruction's MMIO read hook fires, the replayer injects the logged `payload2` value.
- **`AsynchronousInput` / `ExceptionBoundary`**: The replayer injects the interrupt into the GIC before stepping the next instruction, guaranteeing the interrupt is accepted at the exact vector offset recorded.
