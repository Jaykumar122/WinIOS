# WinIOS Campaign 205 — Stage 205.11 Report
## Timer and Interrupt Boot Services Integration

**Stage:** 205.11  
**Status:** COMPLETE / PASS  
**Date:** 2026-11-16  
**Host:** Windows 11 x64  
**Compiler:** Visual Studio 2026 / MSVC C++20 (`/W4 /WX /permissive-`)  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)

---

### 1. Architectural Summary

During Darwin/XNU kernel boot, early operating system setup configures the interrupt controller (GICv3 / AIC) and the ARM64 generic timer:
1. **Interrupt Distributor Initialization:**
   - `GICD_CTLR` enables Group 0 and Group 1 Non-Secure interrupt distribution.
2. **Per-Core Redistributor Initialization:**
   - `GICR_IGROUPR0`: Configures SGIs and PPIs to Group 1 Non-Secure.
   - `GICR_ISENABLER0`: Enables timer PPIs (PPI 27 = CNTV, PPI 30 = CNTP).
3. **CPU Interface (ICC) System Registers:**
   - `ICC_PMR_EL1`: Configured with 0xFF to unmask all priority levels.
   - `ICC_IGRPEN1_EL1`: Enabled to allow Group 1 interrupts to signal the core.
   - `ICC_BPR1_EL1`: Initialized for priority grouping.
4. **ARM Generic Timer Integration:**
   - Frequency programmed in `CNTFRQ_EL0` (24 MHz default architectural frequency).
   - Monotonic time advancement drives timer countdowns.
   - Assertions on PPI 27 / PPI 30 are routed into GICv3 redistributors.
   - Interrupt acknowledgment (`ICC_IAR1_EL1`) and End-of-Interrupt (`ICC_EOIR1_EL1`) protocol handshakes verified.

### 2. Implementation Deliverables

- **`src/cpu/kernel_boot_timer_interrupt.h` & `.cpp`:**
  - `KernelBootTimerConfig` and `KernelBootInterruptConfig`: Encapsulates frequency, tick interval, core topology, and priority parameters.
  - `KernelBootTimerInterruptManager`: Manages the lifecycle of GICv3 and VirtualTime, orchestrating distributor setup, redistributor register writes, timer re-arming, interrupt polling, acknowledgment, and deactivation.
- **`tests/unit/stage205_11_timer_interrupt_boot_services_tests.cpp`:**
  - Comprehensive unit test validating:
    1. Clean initialization across 6 cores.
    2. Virtual timer countdown with accurate assertion of PPI 27 at the designated microsecond threshold.
    3. Proper hardware handshake via `ICC_IAR1_EL1` and `ICC_EOIR1_EL1` with timer re-arming.
    4. Physical timer PPI 30 operation.

### 3. Verification & Test Results

- All 4 test groups passed with zero failures and zero compiler warnings under `/W4 /WX /permissive-`.
- Tested in Release build via CTest and standalone runner.
