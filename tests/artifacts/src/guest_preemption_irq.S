// ============================================================================
// WinIOS Campaign 194 Stage 194.10: Preemptive Timer IRQ Handler
// PPI 27 (Virtual Timer) Vector Routine
// ============================================================================

.text
.global _irq_preemption_handler
.p2align 2

_irq_preemption_handler:
    // 1. Save caller registers
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x16, x30, [sp, #-16]!

    // 2. Acknowledge Interrupt: read ICC_IAR1_EL1
    mrs     x0, ICC_IAR1_EL1

    // 3. Complete Interrupt: write ICC_EOIR1_EL1
    msr     ICC_EOIR1_EL1, x0

    // 4. Reprogram Virtual Timer: write CNTV_TVAL_EL0
    mov     x1, #10000
    msr     CNTV_TVAL_EL0, x1

    // 5. Increment Tick Counter at guest RAM 0x40000
    movz    x2, #0x4, lsl #16
    ldr     x3, [x2]
    add     x3, x3, #1
    str     x3, [x2]

    // 6. Restore caller registers
    ldp     x16, x30, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16

    // 7. Architectural Exception Return
    eret
