# WinIOS Phase 52 Stage 12: Interrupt Delivery and GIC Feature Inventory

## 1. Feature Classification Matrix

| Feature | Architectural Category | Implemented in WinIOS | Tracing in Stage 12 | Exact Detection Point | Retirement Behavior | Exception Generated |
| :--- | :--- | :---: | :---: | :--- | :--- | :--- |
| **Physical Timer (CNTP, INTID 30)** | ARM Generic Timer | **YES** | **YES** | `CPU::step()` at line 4688 / `check_pending_interrupts()` | Suppressed (does NOT retire) | Asynchronous IRQ / FIQ to EL1/EL2/EL3 |
| **Virtual Timer (CNTV, INTID 27)** | ARM Generic Timer | **YES** | **YES** | `CPU::step()` at line 4688 / `check_pending_interrupts()` | Suppressed (does NOT retire) | Asynchronous IRQ / FIQ to EL1/EL2/EL3 |
| **Hypervisor Timer (CNTHP, INTID 26)** | ARM Generic Timer | **YES** | **YES** | `CPU::step()` at line 4688 / `check_pending_interrupts()` | Suppressed (does NOT retire) | Asynchronous IRQ to EL2 |
| **PMU Overflow (INTID 23)** | ARM PMUv3 | **YES** | **YES** | `CPU::step()` at line 4688 / `check_pending_interrupts()` | Suppressed (does NOT retire) | Asynchronous IRQ to EL1 |
| **GICv3 SPIs (INTID 32..127)** | GICv3 Distributor | **YES** | **YES** | `check_pending_interrupts()` via `gic_->evaluate()` | Suppressed (does NOT retire) | Asynchronous IRQ / FIQ |
| **GICv3 SGIs (INTID 0..15)** | GICv3 Redistributor | **YES** | **YES** | `check_pending_interrupts()` via `gic_->evaluate()` | Suppressed (does NOT retire) | Asynchronous IRQ / FIQ |
| **GICv3 Virtual Interrupts** | GICv3 Virtual CPU Interface | **YES** | **YES** | `check_pending_interrupts()` via `ich_lr` evaluation | Suppressed (does NOT retire) | Asynchronous vIRQ / vFIQ to EL1 |
| **FIQ Delivery (Group 0 / Secure)** | ARM GICv3 / Security | **YES** | **YES** | `check_pending_interrupts()` when `is_fiq == true` | Suppressed (does NOT retire) | Asynchronous FIQ to EL1/EL2/EL3 |
| **EL2 Hypervisor Routing** | ARM Virtualization | **YES** | **YES** | `check_pending_interrupts()` when `HCR_EL2.IMO/FMO == 1` | Suppressed (does NOT retire) | Asynchronous exception to EL2 |
| **EL3 Secure Monitor Routing** | ARM TrustZone | **YES** | **YES** | `check_pending_interrupts()` when `SCR_EL3.IRQ/FIQ == 1` | Suppressed (does NOT retire) | Asynchronous exception to EL3 |
| **WFI / WFE Interrupt Wakeup** | Architectural Wait State | **YES** | **YES** | `CPU::poll_wait_wakeup()` at line 4176 | WFI exits; pending instruction vectors | Asynchronous exception to target EL |
| **Spurious Interrupts (INTID 1023)** | GICv3 Controller | **YES** | **NO** | `GICv3::read_icc_iar0/1_el1()` | Normal execution; no vectoring | None (returns 1023 to software) |
| **ICC Register Access (`MRS`/`MSR`)** | System Register Interface | **YES** | **Stage 9** | Handled via `SystemRegisterAccess` (Type 6) | Normal retirement | None |
| **LPIs (Locality-specific / ITS)** | GICv3 / ITS | **NO** | **NO** | Not implemented in WinIOS GICv3 | N/A | N/A |
| **Legacy External Interrupt Pins** | Board / SoC Pins | **NO** | **NO** | Not implemented (all routed via GIC/Timer) | N/A | N/A |

---

## 2. Detailed Breakdown of Supported Interrupt Sources

### 2.1 ARM Generic Timer Interrupts
- **Architectural Timer PPIs**:
  - `INTID 30`: EL1 Physical Timer (`CNTP_CTL_EL0.ENABLE == 1 && CNTP_CTL_EL0.IMASK == 0 && count >= CNTP_CVAL_EL0`).
  - `INTID 27`: EL1 Virtual Timer (`CNTV_CTL_EL0.ENABLE == 1 && CNTV_CTL_EL0.IMASK == 0 && count >= CNTV_CVAL_EL0`).
  - `INTID 26`: EL2 Hypervisor Physical Timer (`CNTHP_CTL_EL2.ENABLE == 1 && CNTHP_CTL_EL2.IMASK == 0 && count >= CNTHP_CVAL_EL2`).
- **Delivery Evaluation**:
  - Evaluated in `check_pending_interrupts()` directly or via `GICv3::set_ppi_level()`.
  - When timer condition asserts, signal is checked against `PSTATE.DAIF.I` (or `F`).
- **Trace Event Representation**:
  - `event_type`: `TraceEventType::InterruptDelivered` (5).
  - `pc`: Interrupted instruction PC (`source_pc`).
  - `address`: Exception vector entry point (`VBAR_ELx + offset`).
  - `data`: INTID (30, 27, or 26).
  - `opcode_or_syndrome`: `(is_fiq ? 1 : 0) | (is_virtual ? 2 : 0) | (priority << 8)`.
  - `flags`: `IsInterrupt | HasVirtualAddress | HasData | (target_el > 0 ? IsPrivileged : None)`.
  - `core_id`: Target CPU core index (0..3).
  - `exception_level`: Target Exception Level (1, 2, or 3).

### 2.2 ARM PMUv3 Overflow Interrupts
- **PMU Overflow PPI**:
  - `INTID 23`: PMU counter overflow line (`pmu_.has_pending_interrupt() == true`).
  - Triggered when cycle counter or event counter overflows and `PMINTENSET_EL1` has the corresponding bit enabled.
- **Trace Event Representation**:
  - `data`: `23`.
  - `opcode_or_syndrome`: Encoded priority, non-FIQ (`is_fiq = 0`).

### 2.3 GICv3 Shared Peripheral Interrupts (SPIs)
- **SPI Range**: INTIDs 32 to 127.
- **Triggering & Routing**:
  - Asserted via `GICv3::set_spi_level(intid, level)`.
  - Routed to target core according to `GICD_IROUTERn` or affinity routing.
  - Filtered by `GICD_ISENABLERn`, `GICD_CTLR.EnableGrp1`, `ICC_PMR_EL1`, and `ICC_IGRPEN1_EL1`.
- **Trace Event Representation**:
  - `data`: `intid` (32..127).
  - `opcode_or_syndrome`: `(is_fiq ? 1 : 0) | (is_virtual ? 2 : 0) | (priority << 8)`.

### 2.4 GICv3 Software Generated Interrupts (SGIs)
- **SGI Range**: INTIDs 0 to 15.
- **Triggering & Routing**:
  - Generated via `ICC_SGI1R_EL1` system register write.
  - Per-core pending state in `GICR_ISPENDR0`.
- **Trace Event Representation**:
  - `data`: `intid` (0..15).

### 2.5 GICv3 Virtual Interrupts
- **Virtual Interface**:
  - Programmed via EL2 List Registers (`ICH_LR<0..3>_EL2`).
  - Injected directly into guest OS running at EL1/EL0.
- **Trace Event Representation**:
  - `data`: Virtual INTID from `ICH_LRn_EL2.vINTID`.
  - `opcode_or_syndrome`: `is_virtual` bit (bit 1) set to 1.

---

## 3. Masking & Preemption Rules

1. **DAIF Masking**:
   - `PSTATE.DAIF.I` masks normal IRQ delivery when `current_el == target_el`.
   - `PSTATE.DAIF.F` masks FIQ delivery when `current_el == target_el`.
   - If `current_el < target_el` (e.g. EL0 running, interrupt targeting EL1), DAIF masking at EL0 is bypassed architecturally.
2. **GIC Priority Masking**:
   - `ICC_PMR_EL1` defines the priority cutoff. An interrupt with `priority >= ICC_PMR_EL1` is masked at the controller and **never delivered to the CPU**.
3. **Running Priority & Preemption**:
   - If CPU is already executing inside an ISR (active interrupt with running priority), a new interrupt is delivered only if its priority is higher than the running priority and satisfies the binary point preemption split (`ICC_BPR0/1_EL1`).
4. **Suppression of Spurious Interrupts**:
   - If no interrupt meets delivery criteria, the CPU continues instruction execution. Reads to `ICC_IAR0/1_EL1` returning 1023 do not trigger delivery events.

---

## 4. Unsupported or Out-of-Scope Features

| Feature | Reason for Non-Support | Impact on Stage 12 |
| :--- | :--- | :--- |
| **GICv3 ITS / LPIs** | Not modeled in WinIOS architecture. | None; SPI/PPI/SGI cover all virtualized hardware paths. |
| **External SoC Pins** | WinIOS is an AArch64 virtual machine hypervisor/emulator. | All interrupts originate from internal virtual timers, PMU, or simulated peripherals. |
| **Asynchronous Aborts (SError)** | SError exceptions are classified under System/Aborts rather than standard IRQ/FIQ delivery. | Handled under `TraceEventType::ExceptionEntered` with abort syndromes. |
