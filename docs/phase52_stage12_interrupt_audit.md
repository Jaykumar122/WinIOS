# WinIOS Phase 52 Stage 12: Interrupt Delivery and GIC Architecture Audit

## 1. Executive Summary & Objective

This audit inspects all interrupt pathways, CPU acceptance boundaries, masking mechanisms, and GIC (Generic Interrupt Controller) components present in the WinIOS codebase. The objective of Phase 52 Stage 12 is to instrument the accepted delivery of architectural interrupts using:
- `TraceEventType::InterruptDelivered` (Type 5)

The trace event must be emitted **only when an interrupt has passed all controller selection, routing, and CPU masking checks and is accepted for delivery to the target CPU**, immediately preceding the architectural exception vector entry.

---

## 2. Distinction of Interrupt Lifecycle States

A critical requirement of this stage is to strictly distinguish the 7 distinct phases of interrupt processing:

| Phase | Description | Observable State in WinIOS | Generates `InterruptDelivered`? |
| :--- | :--- | :--- | :---: |
| **1. Asserted** | An interrupt source (e.g. Generic Timer line, PMU overflow, or peripheral) asserts its physical/virtual line high. | `virtual_time_->get_phys_timer_irq()`, `pmu_.has_pending_interrupt()`, or `gic.set_spi_level()`. | **NO** |
| **2. Pending** | The interrupt controller records the interrupt as pending in its internal status registers. | `GICR_ISPENDR0`, `GICD_ISPENDRn`, or active level line. | **NO** |
| **3. Eligible for Delivery** | The interrupt is enabled in the controller, its priority is higher than PMR, and it can preempt running priority. | `GICv3::evaluate()` determines `has_interrupt == true`. | **NO** |
| **4. Selected by Controller** | Controller selects the highest-priority eligible interrupt for a target CPU. | `PendingSignal sig` with specific `intid` and `is_fiq`. | **NO** |
| **5. Acknowledged by Software** | Guest software reads `ICC_IAR0_EL1` or `ICC_IAR1_EL1` inside the ISR, transitioning state to active. | Handled via `MRS` instruction (traced in Stage 9 as `SystemRegisterAccess`). | **NO** |
| **6. Accepted by CPU** | The CPU checks routing and DAIF masks. If unmasked or lower EL, the CPU **accepts** the interrupt for delivery. | `check_pending_interrupts()` returns `true` and vectors CPU. | **YES (Exactly 1 Event)** |
| **7. Exception Entered** | CPU enters the vector base (`VBAR_ELx + offset`), updates SPSR, ELR, and masks DAIF. | `raise_*_asynchronous_exception()` executes vector branch. | Emits `ExceptionEntered` |

---

## 3. CPU Interrupt Handling Subsystem

### 3.1 Detection & Acceptance Point
In `src/cpu/cpu.cpp`, interrupt acceptance occurs at:
1. **Instruction Boundary in `CPU::step()`** (line 4688):
   ```cpp
   if (architectural_exceptions_) {
       if (check_pending_interrupts()) {
           return;
       }
   }
   ```
   When `check_pending_interrupts()` accepts an interrupt:
   - `CPU::step()` returns immediately before instruction fetch or execution.
   - `StepCounterGuard` destructor sees `retired == false`.
   - **Zero** `InstructionRetired` events are emitted for the pending instruction.
   - `program_counter_` points to the interrupted instruction (saved to `ELR_ELx`).
2. **Wait For Interrupt / Event Wakeup (`poll_wait_wakeup()`, line 4176)**:
   - When CPU is in `WFI` state (`waiting_for_interrupt_ == true`):
     - `poll_wait_wakeup()` checks `check_pending_interrupts()`.
     - If accepted, CPU exits WFI state and vectors to the interrupt handler.
3. **WFE / WFI Instruction Execution (lines 7102, 7124)**:
   - If an interrupt is already pending and unmasked when WFE/WFI executes, CPU increments PC past WFE/WFI (`PC = PC + 4`), saves next PC to ELR, and takes the asynchronous exception without entering sleep.

### 3.2 DAIF Masking Rules
- In AArch64, DAIF bit 1 is `I` (IRQ mask) and bit 0 is `F` (FIQ mask).
- **Masking at Same EL**: If the interrupt targets the current EL, it is delivered only if the corresponding DAIF mask bit is 0 (`!(daif_ & mask)`).
- **Unmasked from Lower EL**: If the interrupt targets a higher EL (e.g. EL0 executing, interrupt targets EL1, EL2, or EL3), the interrupt is **always eligible and accepted**, ignoring the lower EL's DAIF mask (`current_el_ < target_el`).
- Masked interrupts remain pending in the GIC and timer. When software clears the DAIF mask (`MSR DAIFClr`), the next boundary check accepts delivery.

### 3.3 Exception-Level Routing
Routing follows architectural AArch64 configuration:
- **EL3 Routing**: Evaluated via `SCR_EL3.IRQ` (bit 1) and `SCR_EL3.FIQ` (bit 2). If set, physical interrupts are routed directly to EL3 via `raise_el3_asynchronous_exception`.
- **EL2 Hypervisor Routing**: Evaluated via `HCR_EL2.IMO` (bit 4) and `HCR_EL2.FMO` (bit 3). If set, physical interrupts are routed to EL2 via `raise_el2_asynchronous_exception`.
- **EL1 OS Routing**: Default for normal OS execution when hypervisor/secure routing is disabled. Enters EL1 via `raise_asynchronous_exception`.

---

## 4. GICv3 Controller Implementation

### 4.1 Architecture & Register Layout
WinIOS implements an architectural GICv3 emulation model (`winios::GICv3`):
- **GICD (Distributor)** at `0x08000000` (64 KB range):
  - `GICD_CTLR`: Enable Group 0 (bit 0) and Enable Group 1 (bit 1).
  - `GICD_ISENABLERn` / `GICD_ICENABLERn`: Interrupt enable bitmasks.
  - `GICD_ISPENDRn` / `GICD_ICPENDRn`: Interrupt pending bitmasks.
  - `GICD_ISACTIVERn` / `GICD_ICACTIVERn`: Interrupt active bitmasks.
  - `GICD_IPRIORITYRn`: 8-bit priority per SPI (32..127).
  - `GICD_IROUTERn`: Affinity routing and IRM (Interrupt Routing Mode) for SPIs.
- **GICR (Redistributor)** at `0x080A0000` (128 KB per core):
  - `RD_base`: `GICR_CTLR`, `GICR_WAKER`.
  - `SGI_base`: `GICR_IGROUPR0`, `GICR_ISENABLER0`, `GICR_ISPENDR0`, `GICR_ISACTIVER0`, `GICR_IPRIORITYRn` for SGIs (0..15) and PPIs (16..31).
- **CPU Interface Registers (ICC, System Registers)**:
  - `ICC_PMR_EL1`: Priority Mask Register (interrupts with priority $\ge$ PMR are masked).
  - `ICC_BPR0_EL1` / `ICC_BPR1_EL1`: Binary Point Registers for preemption.
  - `ICC_IGRPEN0_EL1` / `ICC_IGRPEN1_EL1`: Group 0 and Group 1 enable.
  - `ICC_IAR0_EL1` / `ICC_IAR1_EL1`: Interrupt Acknowledge Registers.
  - `ICC_EOIR0_EL1` / `ICC_EOIR1_EL1`: End of Interrupt Registers.
  - `ICC_RPR_EL1`: Running Priority Register.
- **Virtual Interface Registers (ICH, EL2)**:
  - `ICH_HCR_EL2`, `ICH_VMCR_EL2`, `ICH_LR<0..3>_EL2` for virtual interrupt injection into EL1/EL0.

### 4.2 Priority & Preemption
- GICv3 uses lower numerical values for higher architectural priorities (`0x00` = highest, `0xFF` = lowest).
- An interrupt is eligible only if `priority < ICC_PMR_EL1` and `can_preempt_priority(priority, running_priority, bpr)`.

### 4.3 Spurious Interrupts
- If no interrupt is pending or eligible when software reads `ICC_IAR0_EL1` or `ICC_IAR1_EL1`, the controller returns `1023` (`kSpuriousIntId`).
- Spurious acknowledgements do **not** generate delivery events.

---

## 5. Trace Event Schema Mapping for `InterruptDelivered`

The existing fixed 64-byte `TraceEvent` schema represents `InterruptDelivered` without modification:

| `TraceEvent` Field | Value / Representation | Description |
| :--- | :--- | :--- |
| `sequence_number` | `++instruction_trace_sequence_` | Monotonic per-core sequence number |
| `virtual_time` | `virtual_time_->get_physical_counter()` | Architectural timestamp (nanoseconds / counter ticks) |
| `pc` | `source_pc` | Program counter of the interrupted instruction |
| `address` | `vector_address` | Exception vector target PC (`VBAR_ELx + offset`) |
| `data` | `intid` | AArch64 Interrupt ID (0..1023) |
| `opcode_or_syndrome` | Encoded metadata | Bit 0: `is_fiq ? 1 : 0`<br>Bit 1: `is_virtual ? 1 : 0`<br>Bits [15:8]: priority |
| `flags` | Bitmask | `IsInterrupt \| HasVirtualAddress \| HasData \| (target_el > 0 ? IsPrivileged : None)` |
| `event_type` | `5` | `TraceEventType::InterruptDelivered` |
| `core_id` | `core_id_` | Target CPU / Core identifier (0..3) |
| `exception_level` | `target_el & 0x3` | Exception level receiving the interrupt (1, 2, or 3) |
| `address_space` | `TraceAddressSpace::Virtual` | Vector address classification |
| `access_size` | `0` | Not applicable for interrupts |
| `reserved` | `0` | Fixed schema invariant |

---

## 6. Event Ordering & Causal Chains

1. **Normal Accepted Interrupt at Instruction Boundary**:
   - `InterruptDelivered` (seq $N$): `pc = source_pc`, `address = vector_pc`, `data = intid`.
   - `ExceptionEntered` (seq $N+1$): `pc = source_pc`, `address = vector_pc`, `flags = IsInterrupt`.
   - The pending instruction is **not** retired; zero `InstructionRetired` events are emitted.
2. **Instruction Retirement Preceding Interrupt**:
   - If an instruction executed prior to the boundary:
   - `InstructionRetired` (seq $N-1$): `pc = prev_pc`.
   - `InterruptDelivered` (seq $N$): `pc = next_pc`.
   - `ExceptionEntered` (seq $N+1$): `pc = next_pc`.
3. **Interrupt Return**:
   - ISR finishes and executes `ERET`.
   - `InstructionRetired` (seq $K$): `pc = eret_pc`.
   - `ExceptionReturned` (seq $K+1$): `pc = eret_pc`, `address = source_pc`.
4. **Masked Interrupt**:
   - Interrupt asserted and pending in GIC.
   - DAIF mask blocks delivery.
   - Zero `InterruptDelivered` and zero `ExceptionEntered` events emitted.
   - CPU executes instructions normally, emitting `InstructionRetired`.
   - When software unmasks DAIF, exactly one `InterruptDelivered` event is emitted.

---

## 7. Audit Checklist & Verification Requirements

- [x] Inspection of `CPU::step()` interrupt boundary.
- [x] Inspection of `check_pending_interrupts()` acceptance logic.
- [x] Inspection of DAIF masking across EL0, EL1, EL2, and EL3.
- [x] Inspection of GICv3 Distributor and Redistributor state.
- [x] Inspection of GICv3 CPU interface registers (ICC) and acknowledgement.
- [x] Mapping of INTIDs (PPIs 23, 26, 27, 30 and SPIs 32..127).
- [x] Verification that spurious interrupt 1023 does not trigger delivery events.
- [x] Verification of SPSC single-producer thread ownership.
- [x] Verification that `TraceEventType::InterruptDelivered` filtering operates independently.
