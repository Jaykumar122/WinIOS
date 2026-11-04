# WinIOS Phase 52 Stage 18: Nondeterministic Event Inventory & Taxonomy

## 1. Overview

In deterministic execution theory, an execution environment is strictly deterministic if every state transition $S_{t+1} = \delta(S_t, I_t)$ is uniquely determined by current state $S_t$ and input $I_t$. In an AArch64 virtual machine, the vast majority of operations (register arithmetic, logical operations, deterministic memory loads/stores to RAM) depend solely on the CPU state and memory state.

Nondeterminism arises exclusively when the CPU core interacts with external, asynchronous, or environmental inputs that cannot be predicted from architectural registers and RAM contents.

This document presents the complete inventory, classification, and hook audit for all nondeterministic sources in WinIOS.

---

## 2. Event Taxonomy

Each nondeterministic event is assigned a fixed enumeration value in `winios::sndl::SndlEventType`:

| Event Type | Numeric Value | Classification | Description |
|---|---|---|---|
| `Unknown` | 0 | Invalid | Uninitialized record placeholder |
| `MmioReadResult` | 1 | Synchronous / Memory | Value returned from reading an MMIO device register |
| `ExternalInterruptInject`| 2 | Asynchronous / Interrupt | External hardware interrupt signaled to GIC |
| `InterruptDelivery` | 3 | Asynchronous / Boundary | Exact retirement boundary where CPU accepts an interrupt |
| `TimerExpiration` | 4 | Temporal / Asynchronous | Hardware/virtual timer expiration event |
| `CounterReadResult` | 5 | Architectural / Register | High-resolution physical/virtual timer counter read |
| `EntropyResult` | 6 | External / RNG | Hardware random number generator result |
| `HostInput` | 7 | Host Interop | External host-provided input payload / packet |
| `ExternalDeviceInput` | 8 | Peripheral / Device | Direct peripheral device transfer / DMA injection |
| `OtherExternalInput` | 9 | Miscellaneous | Generic external input |

---

## 3. Detailed Source Inventory & Capture Precision

### 3.1 MMIO Read Results (`SndlEventType::MmioReadResult`)
- **Hook Location**: `src/cpu/cpu.cpp` within `MemoryAccessor::read()`.
- **Trigger**: Any load instruction targeting a physical address mapped as MMIO (`!paddr_is_ram(paddr)`).
- **Captured Data**:
  - `paddr` (`payload1`): 64-bit physical device register address.
  - `value` (`payload2`): Exact value returned by device model (zero-extended).
  - `access_width` (`payload3`): Access width in bytes (1, 2, 4, 8).
  - `source_id` (`source_id`): Device identifier or peripheral block code.
  - `boundary`: `DuringInstruction`.
- **Replay Value**: On deterministic replay, MMIO read hooks intercept device access and immediately feed `payload2` without invoking the device model, ensuring exact repeatability even if host state differs.

### 3.2 External Interrupt Injection (`SndlEventType::ExternalInterruptInject`)
- **Hook Location**: `src/cpu/gicv3.cpp` within `set_spi_level()`.
- **Trigger**: External hardware components or peripherals asserting/deasserting an SPI interrupt.
- **Captured Data**:
  - `intid` (`payload1`): 32-bit GIC Interrupt ID (SPI 32–1019).
  - `priority` (`payload2`): Interrupt priority level.
  - `flags` (`payload3`): Bit 0 = asserted level (0 or 1), Bit 1 = edge/level triggered.
  - `source_id` (`source_id`): Injecting peripheral subsystem identifier.
  - `boundary`: `AsynchronousInput`.

### 3.3 Accepted Interrupt Delivery (`SndlEventType::InterruptDelivery`)
- **Hook Location**: `src/cpu/cpu.cpp` in `raise_asynchronous_exception()`, `raise_el2_asynchronous_exception()`, and `raise_el3_asynchronous_exception()`.
- **Trigger**: CPU core evaluates pending interrupts in `check_pending_interrupts()` and vector-routes execution to an exception vector (EL1/EL2/EL3).
- **Captured Data**:
  - `intid` (`payload1`): Accepted interrupt ID (PPI/SPI).
  - `priority` (`payload2`): Accepted interrupt priority.
  - `flags` (`payload3`): Bit 0 = `is_fiq`, Bit 1 = `is_virtual`, Bits 8–15 = `target_el`.
  - `pc`: Source PC where execution was interrupted.
  - `ret_count`: Current core instruction retirement count.
  - `boundary`: `ExceptionBoundary`.
- **Replay Value**: Deterministic replay requires interrupting execution at the *exact* instruction count where it occurred originally.

### 3.4 Timer Expiration (`SndlEventType::TimerExpiration`)
- **Hook Location**: `src/cpu/cpu.cpp` in `emit_timer_event_trace()` when `op == TimerOp::Expired`.
- **Trigger**: Virtual timer comparator matches or exceeds counter threshold (`counter >= cval`).
- **Captured Data**:
  - `timer_type` (`payload1`): Physical (0), Virtual (1), Hypervisor (2).
  - `cval` (`payload2`): Comparator match value.
  - `counter` (`payload3`): Current counter timestamp at expiration.
  - `boundary`: `AsynchronousInput`.

### 3.5 High-Resolution Counter Reads (`SndlEventType::CounterReadResult`)
- **Hook Location**: `src/cpu/cpu.cpp` during system register reads for `CNTPCT_EL0` and `CNTVCT_EL0`.
- **Trigger**: Guest execution executes `MRS Xt, CNTPCT_EL0` or `MRS Xt, CNTVCT_EL0`.
- **Captured Data**:
  - `reg_key` (`payload1`): Architectural system register encoding.
  - `counter_val` (`payload2`): 64-bit counter value returned to guest register.
  - `frequency` (`payload3`): Clock frequency (`CNTFRQ_EL0`).
  - `boundary`: `DuringInstruction`.
- **Replay Value**: Guarantees identical timing-based decisions and spin-loop durations during replay.

### 3.6 Hardware Entropy / RNG (`SndlEventType::EntropyResult`)
- **Hook Location**: Cryptographic random number registers or TRNG peripheral models.
- **Trigger**: System register read of `RNDR` / `RNDRRS` or access to hardware TRNG device.
- **Captured Data**:
  - `entropy_value` (`payload1`): 64-bit random value returned.
  - `bit_width` (`payload2`): Bit width (e.g. 64).
  - `source_id` (`source_id`): Entropy provider ID.
  - `boundary`: `DuringInstruction`.

### 3.7 Host Inputs & External Packets (`SndlEventType::HostInput`)
- **Hook Location**: Virtual console, virtio-net, or host-to-guest communication channels.
- **Trigger**: Host pushes console keystrokes, serial bytes, or network packets into guest memory.
- **Captured Data**:
  - `channel_id` (`payload1`): Channel or device queue ID.
  - `payload_data` (`payload2`): Inlined data payload (first 8 bytes).
  - `length` (`payload3`): Total payload length.
  - `boundary`: `BetweenInstructions`.
