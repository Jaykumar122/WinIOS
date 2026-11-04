# Timer & VirtualTime Determinism Validation

## 1. Architectural Overview: ARM Generic Timer & VirtualTime

In an AArch64 virtual machine, the ARM Generic Timer provides uniform timekeeping across EL0 through EL3. The timer subsystem consists of:
1. **System Counter**: A 64-bit monotonically incrementing counter operating at a fixed frequency configured by `CNTFRQ_EL0` (typically 24 MHz or 62.5 MHz in WinIOS).
2. **Physical Timers**:
   - EL1 Non-secure Physical Timer: `CNTP_CTL_EL0`, `CNTP_CVAL_EL0`, `CNTP_TVAL_EL0` (PPI 30).
   - EL2 Hypervisor Physical Timer: `CNTHP_CTL_EL2`, `CNTHP_CVAL_EL2`, `CNTHP_TVAL_EL2` (PPI 26).
3. **Virtual Timers**:
   - EL1 Virtual Timer: `CNTV_CTL_EL0`, `CNTV_CVAL_EL0`, `CNTV_TVAL_EL0`, offset `CNTVOFF_EL2` (PPI 27).

---

## 2. VirtualTime Deterministic Progression Engine

In standard execution, hardware timers query the host operating system clock. In deterministic replay, all time advancement is mediated through the `winios::VirtualTime` engine:

$$\text{Current Ticks} = \text{Base Ticks} + \left\lfloor \frac{\text{Instructions Executed} \times \text{CNTFRQ}}{\text{Emulated Instructions Per Second}} \right\rfloor$$

$$\text{Virtual Nanoseconds} = \left\lfloor \frac{\text{Current Ticks} \times 10^9}{\text{CNTFRQ}} \right\rfloor$$

### Timer Expiration Semantics
A timer generates an active interrupt signal when:
1. `CTL.ENABLE` (bit 0) is 1.
2. `CTL.IMASK` (bit 1) is 0 (unmasked).
3. The counter satisfies the comparator condition:
   $$\text{Counter} \ge \text{CVAL}$$
4. Upon expiration, `CTL.ISTATUS` (bit 2) is asserted to 1, and the associated PPI line is asserted to the GIC redistributor.

### TVAL / CVAL Equivalence
When the guest writes to the 32-bit `TVAL` register, the hardware converts it into an absolute 64-bit comparator value `CVAL`:
$$\text{CVAL} = \text{Counter} + \text{SignExtend64}(\text{TVAL})$$
Conversely, reading `TVAL` returns the down-counter delta:
$$\text{TVAL} = \text{Truncate32}(\text{CVAL} - \text{Counter})$$

---

## 3. Verification & Hardened Test Coverage (Test Group D)

Test Group D (`test_group_d_timer_virtualtime` in `tests/unit/replay_hardening_tests.cpp`) verifies the following behaviors:

1. **Step-Based Progression**: Configured `VirtualTime` with base frequency 24 MHz. Verified that calling `vt.advance_by_instructions(100)` advances virtual nanoseconds and counter ticks deterministically.
2. **Comparator Expiration**: Configured `CNTP_CVAL_EL0` to trigger at counter tick 5000. Advanced virtual time to tick 4999 (signal inactive, `ISTATUS = 0`). Advanced to tick 5000: timer condition asserted immediately (`ISTATUS = 1`, PPI 30 asserted).
3. **Interrupt Masking (`IMASK`)**: Set `CNTP_CTL_EL0.IMASK = 1`. Verified that while `ISTATUS` remained 1, the outward PPI signal to the GIC was masked.
4. **Virtual Timer Offset**: Verified that `CNTVCT_EL0 = CNTPCT_EL0 - CNTVOFF_EL2`.
5. **Hypervisor Physical Timer**: Verified `CNTHP` activation and PPI 26 routing.
