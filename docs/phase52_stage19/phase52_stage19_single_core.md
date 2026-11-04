# WinIOS Phase 52, Stage 19 — Single-Core Replay Execution Model

## 1. Single-Core Determinism

Single-core AArch64 execution on WinIOS is strictly deterministic when the following prerequisites are met:

1. **State Baseline Initialization**: All architectural register files ($X_0 \dots X_{30}$), Vector registers ($V_0 \dots V_{31}$), status flags (PSTATE, NZCV, FPCR, FPSR), banked stack pointers ($SP\_EL0, SP\_EL1, SP\_EL2, SP\_EL3$), exception configuration registers ($VBAR, SCTLR, TCR, TTBR, MAIR, HCR, SCR$), guest RAM contents, and interrupt mask bits (DAIF) are restored identically from a verified `.wsnap` snapshot.
2. **Deterministic Stepping**: Instruction execution proceeds via an isolated sequential stepping loop `cpu.step(memory)`.
3. **Deterministic Input Boundary Injection**: All non-deterministic sources that deviate from sequential deterministic arithmetic (MMIO register reads, cycle counters, hardware interrupts, and entropy) are substituted with their exact logged values from `.sndl`.

---

## 2. Input Routing and Interception Mechanics

```
                 +--------------------------------+
                 |  Instruction Stepping (cpu.cpp)|
                 +---------------+----------------+
                                 |
                 +---------------+----------------+
                 | Is memory access in GIC MMIO?  |
                 +---------------+----------------+
                                 |
                    +------------+------------+
                    |                         |
                 [ YES ]                   [ NO ]
                    |                         |
                    v                         v
     +------------------------------+   +-------------------+
     | is_replay_active()?          |   | Normal Guest RAM  |
     +--------------+---------------+   | Memory Read       |
                    |                   +-------------------+
       +------------+------------+
       |                         |
    [ YES ]                   [ NO ]
       |                         |
       v                         v
+------------------------+  +-------------------+
| hook_replay_mmio_read  |  | Live GIC MMIO     |
| - Verify addr & width  |  | register read     |
| - Return logged value  |  +-------------------+
+------------------------+
```

### 2.1 Peripheral Device MMIO Reads

Guest software communicates with hardware devices through MMIO addresses. During recording, reads to these addresses may return varying device statuses (e.g., UART FIFO status, GIC active interrupt acknowledge `GICC_IAR`).

During deterministic replay:
- `hook_replay_mmio_read(core_id, pc, ret_count, pa, width, out_value)` intercepts the read at the exact physical address `pa` and instruction retirement count `ret_count`.
- The live device logic is **bypassed**.
- The recorded value is injected into the destination register directly.
- Benefit: Prevents simulated peripheral state drift from altering control flow during replay.

### 2.2 Architectural Counter Virtualization

System register reads to `CNTPCT_EL0` and `CNTVCT_EL0`:
- Reading hardware timers during live execution introduces host-jitter and timing variations.
- During deterministic replay, `hook_replay_counter_read` replaces the counter with the exact logged timestamp recorded in Stage 18.
- Guest loops checking timeout expiration or benchmarking inner loops will execute the identical number of iterations.

### 2.3 Guest Memory Load and Store Repeatability

- Because memory is initialized from the validated snapshot, and all stores are strictly deterministic functions of register states, every load from non-MMIO RAM returns the exact same value as during recording.
- The `exclusive_monitor_` reservation mechanism operates deterministically within the single core.

---

## 3. Asynchronous Interrupt and Timer Delivery

External interrupts and timer events arrive asynchronously during recorded execution. During replay:

1. **Retirement Count Matching**:
   Each asynchronous event in the `.sndl` log has an associated `instruction_retirement_count` indicating the boundary at which it was accepted.
2. **Pre-Step Delivery**:
   Prior to fetching the next instruction, `ReplayScheduler` checks if the head of the SNDL log contains an asynchronous event targeted at the current retirement count.
3. **Exception Architecture Invocation**:
   When matched:
   - The interrupt ID (`intid`), priority, FIQ flag, and target exception level (`target_el`) are unpacked.
   - The scheduler invokes `cpu.raise_asynchronous_exception()` or `cpu.raise_el2_asynchronous_exception()` / `cpu.raise_el3_asynchronous_exception()`.
   - The core switches Exception Levels, records return PC into `ELR_ELx`, and sets `program_counter_` to the vector table entry.
   - The event is recorded as consumed and the cursor advances.
