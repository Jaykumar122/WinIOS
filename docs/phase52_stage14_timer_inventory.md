# WinIOS Phase 52 Stage 14: Timer Feature Inventory

## 1. Subsystem Architecture Overview

The WinIOS timer architecture implements the ARMv8-A Generic Timer specification. It provides virtual and physical counter tracking, multiple architectural timer channels per core, trapping mechanisms across Exception Levels (EL0, EL1, EL2), and integration with the GICv3 interrupt controller.

---

## 2. Supported vs. Unsupported Timers

| Timer Channel | Architecture Level | GIC PPI | Supported in WinIOS? | Tracing Supported? |
| :--- | :---: | :---: | :---: | :---: |
| **EL1 Physical Timer (`CNTP`)** | Non-secure EL1 / EL0 | PPI 30 | **YES** | **YES** |
| **EL1 Virtual Timer (`CNTV`)** | Non-secure EL1 / EL0 | PPI 27 | **YES** | **YES** |
| **EL2 Hypervisor Physical Timer (`CNTHP`)** | EL2 (Hypervisor) | PPI 26 | **YES** | **YES** |
| **EL2 Hypervisor Virtual Timer (`CNTHV`)** | EL2 (Hypervisor) | PPI 28 | NO | NO (Explicitly Unsupported) |
| **EL3 Secure Physical Timer (`CNTPS`)** | EL3 (Secure Monitor) | PPI 29 | NO | NO (Explicitly Unsupported) |

---

## 3. Supported System Register Inventory

| Register Name | Key | Op0 | Op1 | CRn | CRm | Op2 | Read Behavior | Write Behavior | Traps Supported |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- | :--- | :--- |
| `CNTFRQ_EL0` | `0x5700` | 3 | 3 | 14 | 0 | 0 | Returns system clock frequency (default 24 MHz) | Sets clock frequency | `CNTKCTL_EL1.EL0PCTEN` |
| `CNTPCT_EL0` | `0x5701` | 3 | 3 | 14 | 0 | 1 | Returns monotonic physical counter | Undefined exception (RO) | `CNTKCTL_EL1`, `CNTHCTL_EL2` |
| `CNTVCT_EL0` | `0x5702` | 3 | 3 | 14 | 0 | 2 | Returns `physical_counter - cntvoff_el2` | Undefined exception (RO) | `CNTKCTL_EL1`, `CNTHCTL_EL2` |
| `CNTKCTL_EL1` | `0x4708` | 3 | 0 | 14 | 1 | 0 | Returns kernel timer control register | Updates kernel timer control | None (EL1 only) |
| `CNTP_TVAL_EL0` | `0x5710` | 3 | 3 | 14 | 2 | 0 | Returns `cval - physical_counter` (signed 32-bit) | Updates CVAL relative to physical counter | `CNTKCTL_EL1`, `CNTHCTL_EL2` |
| `CNTP_CTL_EL0` | `0x5711` | 3 | 3 | 14 | 2 | 1 | Returns `{istatus, imask, enable}` | Updates `{imask, enable}` | `CNTKCTL_EL1`, `CNTHCTL_EL2` |
| `CNTP_CVAL_EL0` | `0x5712` | 3 | 3 | 14 | 2 | 2 | Returns 64-bit comparator value | Updates 64-bit comparator value | `CNTKCTL_EL1`, `CNTHCTL_EL2` |
| `CNTV_TVAL_EL0` | `0x5718` | 3 | 3 | 14 | 3 | 0 | Returns `cval - virtual_counter` (signed 32-bit) | Updates CVAL relative to virtual counter | `CNTKCTL_EL1`, `CNTHCTL_EL2` |
| `CNTV_CTL_EL0` | `0x5719` | 3 | 3 | 14 | 3 | 1 | Returns `{istatus, imask, enable}` | Updates `{imask, enable}` | `CNTKCTL_EL1`, `CNTHCTL_EL2` |
| `CNTV_CVAL_EL0` | `0x571A` | 3 | 3 | 14 | 3 | 2 | Returns 64-bit virtual comparator value | Updates 64-bit virtual comparator value | `CNTKCTL_EL1`, `CNTHCTL_EL2` |
| `CNTVOFF_EL2` | `0x6703` | 3 | 4 | 14 | 0 | 3 | Returns 64-bit virtual counter offset | Updates virtual counter offset | Undefined below EL2 |
| `CNTHCTL_EL2` | `0x6708` | 3 | 4 | 14 | 1 | 0 | Returns hypervisor timer control | Updates hypervisor timer control | Undefined below EL2 |
| `CNTHP_TVAL_EL2` | `0x6710` | 3 | 4 | 14 | 2 | 0 | Returns `cval - physical_counter` | Updates hypervisor CVAL | Undefined below EL2 |
| `CNTHP_CTL_EL2` | `0x6711` | 3 | 4 | 14 | 2 | 1 | Returns `{istatus, imask, enable}` | Updates `{imask, enable}` | Undefined below EL2 |
| `CNTHP_CVAL_EL2` | `0x6712` | 3 | 4 | 14 | 2 | 2 | Returns 64-bit hypervisor comparator | Updates 64-bit hypervisor comparator | Undefined below EL2 |

---

## 4. Timer Event Enumeration & Operation Types

```cpp
namespace winios::trace {

enum class TimerType : std::uint8_t {
    Physical           = 0, // EL1 Physical Timer (CNTP, PPI 30)
    Virtual            = 1, // EL1 Virtual Timer (CNTV, PPI 27)
    HypervisorPhysical = 2  // EL2 Hypervisor Physical Timer (CNTHP, PPI 26)
};

enum class TimerOp : std::uint8_t {
    CompareUpdate      = 1, // CVAL / TVAL modified
    ControlUpdate      = 2, // CTL register modified (ENABLE / IMASK)
    Expired            = 3, // Counter >= CVAL condition met (istatus = 1)
    InterruptAsserted  = 4, // IRQ asserted to GIC PPI
    InterruptCleared   = 5, // IRQ deasserted (masked, disabled, or CVAL updated)
    Reset              = 6  // Timer state reset to initial defaults
};

}
```

---

## 5. Strict Non-Emission Boundaries

1. **Counter Read Non-Emission**:
   - `CNTPCT_EL0` and `CNTVCT_EL0` counter reads do **not** emit `TimerEvent`.
   - Rationale: High-frequency guest polling of counter registers would flood trace buffers with redundant records. Reading counter registers generates `SystemRegisterAccess` (Type 6) if system register tracing is enabled.
2. **Polling Idempotency**:
   - `CPU::check_timer_status()` and `CPU::check_pending_interrupts()` run frequently at instruction boundaries and inside wait loops.
   - When timer state does not transition (`istatus` remains unchanged, `irq_asserted` remains unchanged), **zero** trace events are emitted.
3. **WFI / WFE Idle Advancement**:
   - Advancing the shared monotonic physical counter to the earliest deadline during WFI/WFE idle advancement advances virtual time cleanly.
   - Only the actual expiration and interrupt assertion of the target timer emit trace events.
