# Nondeterministic Source Audit & Taxonomy

## 1. Overview and Problem Definition

In an instruction-level virtual machine, execution is purely deterministic if and only if:
1. The initial architectural state is identical.
2. The instruction execution semantics are strictly deterministic functions of current architectural state.
3. All inputs external to the CPU core (asynchronous signals, peripheral responses, timers, and hardware entropy) are recorded with precise boundary coordinates and reinjected identically during replay.

Any unrecorded or imprecise input causes immediate or latent architectural state divergence, rendering deterministic replay impossible. This document audits every source of nondeterminism in the WinIOS AArch64 execution environment and formalizes its recording and replay mechanics under the Snapshot + Non-Deterministic Log (SNDL) architecture.

---

## 2. Nondeterministic Source Taxonomy

| Source Category | SNDL Event Type | Concrete Manifestation in WinIOS | Injection Point & Boundary Semantics |
|---|---|---|---|
| **MMIO Reads** | `MmioRead` (0x02) | Peripheral register reads (`0x08000000`–`0x08FFFFFF` for GIC, UART, VirtIO). | Injected synchronously inside `Memory::read_*()` when addressing non-RAM physical ranges. Value returned from log instead of host emulation. |
| **Direct Memory Access (DMA)** | `DmaBufferWrite` (0x03) | Peripheral bus master writes directly into guest RAM (e.g. disk block read, network packet reception). | Injected before the instruction boundary that consumes or checks the DMA buffer status register. Memory payload updated in guest RAM. |
| **Device Interrupts** | `InterruptAssert` (0x01) | SPIs (Shared Peripheral Interrupts, IDs 32–1019) and PPIs (Private Peripheral Interrupts, IDs 16–31) signaled by peripherals or timers. | Bound to exact `instruction_index`. Injected at instruction retirement boundary prior to instruction decode of the next cycle. |
| **System Counters** | `CounterRead` (0x04) | Guest execution reads `CNTPCT_EL0`, `CNTVCT_EL0`, or `PMCCNTR_EL0`. | Synchronously intercepted in `CPU::read_system_register()`. The recorded tick value is returned directly to destination register `Xt`. |
| **Hardware Entropy** | `EntropyResult` (0x05) | Guest reads ARMv8.5 random number registers `RNDR` or `RNDRRS` via `MRS`. | Synchronously intercepted in system register handler. Recorded random 64-bit value and success flag injected into `Xt` and `PSTATE.NZCV`. |
| **Asynchronous CPU Signals** | `CpuSignal` (0x06) | PSCI inter-core wakeups (`CPU_ON`, `CPU_OFF`), reset requests, or external debug halt signals. | Injected at instruction retirement boundary, forcing architectural state transition or CPU run-state change. |

---

## 3. Boundary & Sequencing Rules

To guarantee bit-for-bit repeatability, every nondeterministic event record in `.sndl` conforms to strict sequencing invariants:

1. **Instruction-Retirement Boundary**:
   Events that occur asynchronously (interrupts, DMA completion) are committed only at discrete instruction retirement boundaries. An event recorded at `instruction_index = N` takes effect *after* instruction $N$ has fully retired and updated architectural state, but *before* instruction $N+1$ fetches.

2. **Core-Scoped Monotonic Sequence Numbers**:
   Within a given core's event stream, `sequence_num` is strictly monotonically increasing ($1, 2, 3, \dots, M$). During replay, `ReplayInputRouter` verifies that incoming events match the expected sequence number, rejecting gaps, out-of-order records, or duplicates.

3. **Synchronous In-Instruction Routing**:
   Events that occur as a direct consequence of instruction execution (MMIO reads, system counter reads, entropy reads) occur synchronously during the execution of instruction $N$. The router matches the pending event at `instruction_index = N` and supplies the recorded value directly to the execution pipeline.

---

## 4. Verification in Stage 20 Test Suite

Test Group A (`test_group_a_source_coverage` in `tests/unit/replay_hardening_tests.cpp`) systematically exercises each supported nondeterministic record type:
- Counter read injection (`CNTPCT_EL0` emulation with recorded tick values).
- MMIO read interception (returning synthetic device register values).
- Hardware entropy generation (mocking `RNDR` 64-bit results).
- Interrupt line assertions across SPI and PPI vectors.

All inputs were proven to override or bypass default unrecorded host behavior without corrupting internal CPU register invariants.
