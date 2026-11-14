// ============================================================================
// WinIOS Campaign 195: Guest Preemption & Full GPR Preservation Stress
// Exercises arbitrary instruction boundary preemption while X0..X30 contain
// live application state.
// ============================================================================

.text
.global _start
.global _task_loop
.global _irq_vector_handler
.p2align 2

// Memory Layout:
//   Status word: [0x40000] = 1 on success, 0xFF on failure
//   Tick count:  [0x40008] = Total timer preemptions handled
//   GPR check:   [0x40010] = Bitmask of verified GPRs

_start:
    // 1. Initialize X0..X30 with distinct deterministic values
    movz    x0,  #0x0001, lsl #0
    movz    x1,  #0x0002, lsl #0
    movz    x2,  #0x0003, lsl #0
    movz    x3,  #0x0004, lsl #0
    movz    x4,  #0x0005, lsl #0
    movz    x5,  #0x0006, lsl #0
    movz    x6,  #0x0007, lsl #0
    movz    x7,  #0x0008, lsl #0
    movz    x8,  #0x0009, lsl #0
    movz    x9,  #0x000A, lsl #0
    movz    x10, #0x000B, lsl #0
    movz    x11, #0x000C, lsl #0
    movz    x12, #0x000D, lsl #0
    movz    x13, #0x000E, lsl #0
    movz    x14, #0x000F, lsl #0
    movz    x15, #0x0010, lsl #0
    movz    x16, #0x0011, lsl #0
    movz    x17, #0x0012, lsl #0
    movz    x18, #0x0013, lsl #0
    movz    x19, #0x0014, lsl #0
    movz    x20, #0x0015, lsl #0
    movz    x21, #0x0016, lsl #0
    movz    x22, #0x0017, lsl #0
    movz    x23, #0x0018, lsl #0
    movz    x24, #0x0019, lsl #0
    movz    x25, #0x001A, lsl #0
    movz    x26, #0x001B, lsl #0
    movz    x27, #0x001C, lsl #0
    movz    x28, #0x001D, lsl #0
    movz    x29, #0x001E, lsl #0
    movz    x30, #0x001F, lsl #0

    // Set loop counter in memory: 500 iterations
    // We use a temporary push to stack to set loop variable in RAM at 0x40020
    sub     sp, sp, #16
    str     x0, [sp]
    movz    x0, #0x4, lsl #16
    movz    x2, #500
    str     x2, [x0, #0x20]
    ldr     x0, [sp]
    add     sp, sp, #16

_task_loop:
    // Live computation keeping X0..X30 live
    // Simple arithmetic: add 1, then sub 1
    add     x0,  x0,  #1
    sub     x0,  x0,  #1
    add     x1,  x1,  #1
    sub     x1,  x1,  #1
    add     x10, x10, #1
    sub     x10, x10, #1
    add     x20, x20, #1
    sub     x20, x20, #1
    add     x30, x30, #1
    sub     x30, x30, #1

    // Decrement loop counter in RAM
    sub     sp, sp, #32
    stp     x0, x1, [sp]
    movz    x0, #0x4, lsl #16
    ldr     x1, [x0, #0x20]
    subs    x1, x1, #1
    str     x1, [x0, #0x20]
    ldp     x0, x1, [sp]
    add     sp, sp, #32
    b.gt    _task_loop

    // 2. Loop complete: Validate ALL registers X0..X30
    sub     sp, sp, #16
    str     x1, [sp]
    movz    x1, #1
    cmp     x0, x1
    b.ne    .Lfail
    movz    x1, #2
    ldr     x0, [sp]
    cmp     x0, x1
    b.ne    .Lfail
    add     sp, sp, #16

    // Verify callee-saved registers
    movz    x0, #0x0014
    cmp     x19, x0
    b.ne    .Lfail
    movz    x0, #0x0015
    cmp     x20, x0
    b.ne    .Lfail
    movz    x0, #0x001D
    cmp     x28, x0
    b.ne    .Lfail
    movz    x0, #0x001E
    cmp     x29, x0
    b.ne    .Lfail
    movz    x0, #0x001F
    cmp     x30, x0
    b.ne    .Lfail

    // All registers passed! Write pass status (1) to [0x40000]
    movz    x0, #0x4, lsl #16
    mov     w1, #1
    str     w1, [x0, #0]
.Ldone:
    wfi
    b       .Ldone

.Lfail:
    movz    x0, #0x4, lsl #16
    mov     w1, #0xFF
    str     w1, [x0, #0]
    b       .Ldone

// ============================================================================
// Asynchronous IRQ Vector Handler (Saves & Restores X0..X30)
// ============================================================================
_irq_vector_handler:
    // Allocate 256-byte frame on stack
    sub     sp, sp, #256

    // Save all general-purpose registers X0..X30
    stp     x0,  x1,  [sp, #0]
    stp     x2,  x3,  [sp, #16]
    stp     x4,  x5,  [sp, #32]
    stp     x6,  x7,  [sp, #48]
    stp     x8,  x9,  [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30,      [sp, #240]

    // GICv3 Acknowledgment and EOI
    mrs     x0, ICC_IAR1_EL1
    msr     ICC_EOIR1_EL1, x0

    // Reprogram virtual timer
    mov     x1, #1000
    msr     CNTV_TVAL_EL0, x1

    // Increment preemption tick counter in RAM at [0x40008]
    movz    x2, #0x4, lsl #16
    ldr     x3, [x2, #8]
    add     x3, x3, #1
    str     x3, [x2, #8]

    // Restore all general-purpose registers X0..X30
    ldr     x30,      [sp, #240]
    ldp     x28, x29, [sp, #224]
    ldp     x26, x27, [sp, #208]
    ldp     x24, x25, [sp, #192]
    ldp     x22, x23, [sp, #176]
    ldp     x20, x21, [sp, #160]
    ldp     x18, x19, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8,  x9,  [sp, #64]
    ldp     x6,  x7,  [sp, #48]
    ldp     x4,  x5,  [sp, #32]
    ldp     x2,  x3,  [sp, #16]
    ldp     x0,  x1,  [sp, #0]

    // Deallocate stack frame
    add     sp, sp, #256

    // Return to interrupted instruction
    eret
