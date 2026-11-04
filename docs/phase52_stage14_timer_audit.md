# WinIOS Phase 52 Stage 14: Timer Event Trace Integration — Architecture Audit

## 1. Executive Summary

This document performs an exhaustive pre-implementation architectural audit of the AArch64 Generic Timer subsystem, virtual time management, system register interfaces, GICv3 interrupt routing, and event tracing mechanisms in WinIOS. The objective is to define the exact semantic boundaries and data layouts for integrating `TraceEventType::TimerEvent` (Type 13) into the Phase 52 binary tracing infrastructure.

---

## 2. Generic Timer Architecture & Virtual Time Model

### 2.1 VirtualTime Subsystem (`src/cpu/virtual_time.h`, `src/cpu/virtual_time.cpp`)
- **Authoritative System Counter (`CNTPCT_EL0`)**:
  - Represented as a shared, monotonic 64-bit atomic counter (`std::atomic<std::uint64_t> physical_counter_`).
  - Single system counter shared across all virtual cores in an SMP cluster.
  - Frequency defaults to 24,000,000 Hz (24 MHz architectural standard) stored in `std::atomic<std::uint64_t> frequency_`.
  - Incremented on instruction execution, stepped via `advance_physical_counter()`, and wrapped safely modulo $2^{64}$.
- **Virtual Counter (`CNTVCT_EL0`)**:
  - Per-core virtual counter calculated as `CNTVCT = physical_counter_ - cntvoff_el2`.
  - `cntvoff_el2` is stored per-core in `CoreTimerState::cntvoff_el2`.
- **Supported Architectural Timers**:
  1. **EL1 Physical Timer (`CNTP_*`)**:
     - Controlled by `CNTP_CTL_EL0`, `CNTP_CVAL_EL0`, `CNTP_TVAL_EL0`.
     - Generates Private Peripheral Interrupt (PPI) 30.
     - Condition: `CNTPCT_EL0 >= CNTP_CVAL_EL0`.
  2. **EL1 Virtual Timer (`CNTV_*`)**:
     - Controlled by `CNTV_CTL_EL0`, `CNTV_CVAL_EL0`, `CNTV_TVAL_EL0`.
     - Generates PPI 27.
     - Condition: `CNTVCT_EL0 >= CNTV_CVAL_EL0`.
  3. **EL2 Hypervisor Physical Timer (`CNTHP_*`)**:
     - Controlled by `CNTHP_CTL_EL2`, `CNTHP_CVAL_EL2`, `CNTHP_TVAL_EL2`.
     - Generates PPI 26.
     - Condition: `CNTPCT_EL0 >= CNTHP_CVAL_EL2`.
- **Unsupported / Unimplemented Architectural Timers**:
  - Secure Physical Timer (`CNTPS_*`): **Not implemented** in WinIOS.
  - Hypervisor Virtual Timer (`CNTHV_*`): **Not implemented** in WinIOS.
  - Non-secure EL2 Virtual Timer (`CNTHVS_*`): **Not implemented** in WinIOS.

### 2.2 Timer State Structure (`struct TimerState`)
```cpp
struct TimerState {
    bool enable{false};                          // Bit 0 of CTL: Timer enabled
    bool imask{false};                           // Bit 1 of CTL: Interrupt masked
    bool istatus{false};                         // Bit 2 of CTL: Condition satisfied
    std::uint64_t cval{0xFFFFFFFFFFFFFFFFULL};   // 64-bit comparator value
};
```
- **TVAL Semantics**:
  - Reading TVAL: `read_tval(current_counter) = static_cast<int32_t>(cval - current_counter)`.
  - Writing TVAL: `cval = current_counter + static_cast<int64_t>(static_cast<int32_t>(tval_raw))`.
- **Evaluation Rule**:
  - `evaluate(current_counter)`: `istatus = enable && (current_counter >= cval)`.
- **Interrupt Assertion Rule**:
  - `irq_asserted()`: `enable && istatus && !imask`.

---

## 3. System Register Interface Audit

| Register | Encoding (Op0, Op1, CRn, CRm, Op2) | Key | Access | Trap Controls | Implementation Status |
| :--- | :---: | :---: | :---: | :---: | :---: |
| `CNTFRQ_EL0` | (3, 3, 14, 0, 0) | `0x5700` | RW at EL1+, RO at EL0 | `CNTKCTL_EL1` | Supported |
| `CNTPCT_EL0` | (3, 3, 14, 0, 1) | `0x5701` | RO | `CNTKCTL_EL1`, `CNTHCTL_EL2` | Supported |
| `CNTVCT_EL0` | (3, 3, 14, 0, 2) | `0x5702` | RO | `CNTKCTL_EL1`, `CNTHCTL_EL2` | Supported |
| `CNTKCTL_EL1` | (3, 0, 14, 1, 0) | `0x4708` | RW at EL1+ | Traps EL0 accesses to EL1 | Supported |
| `CNTP_TVAL_EL0` | (3, 3, 14, 2, 0) | `0x5710` | RW | `CNTKCTL_EL1`, `CNTHCTL_EL2` | Supported |
| `CNTP_CTL_EL0` | (3, 3, 14, 2, 1) | `0x5711` | RW | `CNTKCTL_EL1`, `CNTHCTL_EL2` | Supported |
| `CNTP_CVAL_EL0` | (3, 3, 14, 2, 2) | `0x5712` | RW | `CNTKCTL_EL1`, `CNTHCTL_EL2` | Supported |
| `CNTV_TVAL_EL0` | (3, 3, 14, 3, 0) | `0x5718` | RW | `CNTKCTL_EL1`, `CNTHCTL_EL2` | Supported |
| `CNTV_CTL_EL0` | (3, 3, 14, 3, 1) | `0x5719` | RW | `CNTKCTL_EL1`, `CNTHCTL_EL2` | Supported |
| `CNTV_CVAL_EL0` | (3, 3, 14, 3, 2) | `0x571A` | RW | `CNTKCTL_EL1`, `CNTHCTL_EL2` | Supported |
| `CNTVOFF_EL2` | (3, 4, 14, 0, 3) | `0x6703` | RW at EL2+ | Hypervisor virtual offset | Supported |
| `CNTHCTL_EL2` | (3, 4, 14, 1, 0) | `0x6708` | RW at EL2+ | Traps EL1 accesses to EL2 | Supported |
| `CNTHP_TVAL_EL2` | (3, 4, 14, 2, 0) | `0x6710` | RW at EL2+ | Hypervisor physical timer TVAL | Supported |
| `CNTHP_CTL_EL2` | (3, 4, 14, 2, 1) | `0x6711` | RW at EL2+ | Hypervisor physical timer CTL | Supported |
| `CNTHP_CVAL_EL2` | (3, 4, 14, 2, 2) | `0x6712` | RW at EL2+ | Hypervisor physical timer CVAL | Supported |

---

## 4. Interrupt Delivery & GIC Correlation

### 4.1 Interrupt Path
1. **Timer Expiration**:
   - `istatus` becomes `true` when `counter >= cval`.
2. **Interrupt Assertion**:
   - When `enable && istatus && !imask`, `irq_asserted()` becomes `true`.
   - Wired to GIC redistributor PPI levels:
     - PPI 30: Physical timer
     - PPI 27: Virtual timer
     - PPI 26: Hypervisor timer
3. **Interrupt Acceptance**:
   - Evaluated against `PSTATE.DAIF` (bit I or F) and GIC PMR/priority mask.
   - If accepted at instruction boundary, the next instruction does not retire; instead, the CPU enters the interrupt vector.
4. **Causal Sequence Chain**:
   ```
   TimerEvent (CompareUpdate / TVAL write)
     └── TimerEvent (Expired: counter >= cval)
           └── TimerEvent (InterruptAsserted: PPI 30/27/26 line high)
                 └── InterruptDelivered (Type 5: accepted by CPU)
                       └── ExceptionEntered (Type 3: vector entered)
                             └── ContextChanged (Type 12: EL/DAIF transition)
   ```

---

## 5. TraceEvent Field Mapping for TimerEvent (Type 13)

The 64-byte `TraceEvent` schema is strictly preserved:
- `event_type`: `TraceEventType::TimerEvent` (13)
- `sequence_number`: Monotonically incrementing per-core sequence number
- `virtual_time`: Current physical counter value (`virtual_time_->get_physical_counter()`)
- `pc`: Program counter of triggering instruction (or interrupted instruction at expiration)
- `address`: Target comparator value (`cval`) or timer control value
- `data`: Current counter value at event time (`current_counter`)
- `opcode_or_syndrome`: Bit-packed timer metadata:
  - Bits [7:0]: `TimerOp` (`1` = CompareUpdate, `2` = ControlUpdate, `3` = Expired, `4` = InterruptAsserted, `5` = InterruptCleared, `6` = Reset)
  - Bits [15:8]: `TimerType` (`0` = Physical, `1` = Virtual, `2` = HypervisorPhysical)
  - Bit [16]: `enable` flag
  - Bit [17]: `imask` flag
  - Bit [18]: `istatus` flag
- `core_id`: Core ID (0..63)
- `exception_level`: Current Exception Level (0..3)
- `flags`:
  - `HasData`: asserted (data contains counter)
  - `IsPrivileged`: asserted if EL > 0
  - `IsWrite`: asserted for CVAL/TVAL/CTL updates
  - `IsInterrupt`: asserted for `InterruptAsserted` events
- `address_space`: `TraceAddressSpace::None`
- `access_size`: 8 bytes (or 4 bytes for 32-bit register updates)
- `reserved`: 0

---

## 6. SPSC Concurrency & Producer Safety

- Each `CPU` instance acts as the sole producer to its dedicated `InMemoryRingBuffer` through `TraceManager`.
- Timer events arising from CPU instructions (MSR writes) or CPU polling (`check_timer_status`, `check_pending_interrupts`) execute on the core's own execution thread.
- Multi-core SMP execution utilizes per-core ring buffers, guaranteeing lock-free SPSC operation with zero cross-core contention.
