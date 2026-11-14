# WINIOS CAMPAIGN 194: STAGE 194.10 REPORT
## Preemptive Scheduling with Timer Interrupts

### 1. Executive Summary
Stage 194.10 integrates the ARM64 virtual timer PPI (INTID 27) and GICv3 interrupt controller with the synthetic kernel priority scheduler to establish genuine preemptive multitasking.
Key deliverables:
- **ARM64 IRQ Exception Handling:** Authentic guest IRQ handler at `VBAR_EL1 + 0x280` (Current EL with SPx) and `VBAR_EL1 + 0x480` (Lower EL AArch64).
- **GICv3 Interrupt Lifecycle:** Acknowledges pending interrupts via `mrs x0, ICC_IAR1_EL1` and completes them via `msr ICC_EOIR1_EL1, x0`.
- **Timer Reprogramming:** Resets virtual timer deadline via `msr CNTV_TVAL_EL0, x1` in guest code.
- **Binary Artifact:** Independently cross-compiled using Zig 0.17.0 (Clang 22.1.8) to Mach-O executable `tests/artifacts/bin/guest_preemption_irq` (50,008 bytes, SHA-256 `9836f774a88af1616fc6beeab1a0732829da91a56829d13447a2678f0f2f4c9d`).
- **Preemptive Return:** Tested with architectural exception delivery from both EL1 (kernel) and EL0 (userspace), returning smoothly via `ERET` to interrupted code paths while managing thread scheduling slices.

---

### 2. Architecture & Design

#### 2.1 Hardware Timer & Interrupt Pipeline
1. **Virtual Timer Configuration (`PreemptionTimer::configure_virtual_timer`):**
   - Enables GIC distributor (`GICD_CTLR`).
   - Configures GIC redistributor for core: sets `GICR_IGROUPR0` and `GICR_ISENABLER0` for PPI 27 (Group 1, Enabled).
   - Configures CPU interface: sets `ICC_PMR_EL1` priority mask to `0xF0` and enables Group 1 signaling via `ICC_IGRPEN1_EL1`.
   - Enables virtual timer in `CNTV_CTL_EL0` (`ENABLE=1`, `IMASK=0`).
2. **Interrupt Vectoring (`PreemptionTimer::setup_vector_table`):**
   - Vectors `VBAR_EL1 + 0x280` (EL1) and `VBAR_EL1 + 0x480` (EL0) to branch directly to the guest IRQ preemption handler.
3. **Guest IRQ Handler Routine (`tests/artifacts/src/guest_preemption_irq.S`):**
   ```arm64
   _irq_preemption_handler:
       stp     x0, x1, [sp, #-16]!
       stp     x2, x3, [sp, #-16]!
       stp     x16, x30, [sp, #-16]!
       mrs     x0, ICC_IAR1_EL1     // Acknowledge interrupt (INTID 27)
       msr     ICC_EOIR1_EL1, x0    // End of interrupt
       mov     x1, #10000
       msr     CNTV_TVAL_EL0, x1    // Reprogram timer
       movz    x2, #0x4, lsl #16
       ldr     x3, [x2]
       add     x3, x3, #1
       str     x3, [x2]             // Increment guest RAM tick counter
       ldp     x16, x30, [sp], #16
       ldp     x2, x3, [sp], #16
       ldp     x0, x1, [sp], #16
       eret                         // Resume interrupted execution
   ```

---

### 3. Verification & Results

Unit test suite `tests/unit/stage194_10_preemption_tests.cpp` executes 6 test cases:
1. `test_irq_handler_machine_code`: Validates 15 ARM64 machine words including `ICC_IAR1_EL1`, `ICC_EOIR1_EL1`, `CNTV_TVAL_EL0`, and `ERET`.
2. `test_vector_table_setup`: Validates encoding of relative branches at vector offsets `0x280` and `0x480`.
3. `test_timer_irq_delivery_and_execution_el1`: Fires PPI 27 while CPU executes an EL1 loop; verifies architectural interrupt vectoring to `0x80280`, handler execution, tick counter increment, and ERET return.
4. `test_timer_irq_delivery_and_execution_el0`: Fires PPI 27 while CPU executes at EL0 (user mode); verifies vectoring to `0x80480`, privilege elevation to EL1, and ERET return back to EL0.
5. `test_timer_irq_scheduler_preemption`: Verifies multi-tick scheduler integration where timer interrupts drive quantum expiration and thread preemption.
6. `test_compiled_macho_binary_artifact`: Confirms independently cross-compiled Mach-O binary artifact existence, size (50,008 bytes), and SHA-256 integrity.

Both Release and Debug configurations build cleanly under `/W4 /WX /permissive-` and pass all 6 tests with zero errors.

---

### 4. Classification
- Source implementations: `GUEST_SCHEDULER` / `SYNTHETIC_KERNEL`
- Binary artifact: `GUEST_EL1`
- Simulation / host harness: `HOST_MODEL`
