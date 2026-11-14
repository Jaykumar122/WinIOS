// ============================================================================
// WinIOS Campaign 194 Stage 194.8: Guest Context Switching Routine
// Routine: cpu_switch_to(old_tcb_ptr, new_tcb_ptr)
// Input:
//   X0: Pointer to outgoing thread's GuestTcb in guest RAM (or 0 if initial)
//   X1: Pointer to incoming thread's GuestTcb in guest RAM
// ============================================================================

.text
.global _cpu_switch_to
.p2align 2

_cpu_switch_to:
    // If old_tcb (X0) is null, skip saving current context (used during bootstrap)
    cbz     x0, .Lload_new

    // Save SP to old_tcb->sp_el1 (offset 0x28)
    mov     x2, sp
    str     x2, [x0, #0x28]

    // Save LR (return address) to old_tcb->pc (offset 0x30)
    str     x30, [x0, #0x30]

    // Save callee-saved registers X19..X30 into old_tcb->gpr[19..30]
    stp     x19, x20, [x0, #0xd8]
    stp     x21, x22, [x0, #0xe8]
    stp     x23, x24, [x0, #0xf8]
    stp     x25, x26, [x0, #0x108]
    stp     x27, x28, [x0, #0x118]
    stp     x29, x30, [x0, #0x128]

.Lload_new:
    // Load SP from new_tcb->sp_el1 (offset 0x28)
    ldr     x2, [x1, #0x28]
    mov     sp, x2

    // Restore callee-saved registers X19..X30 from new_tcb
    ldp     x19, x20, [x1, #0xd8]
    ldp     x21, x22, [x1, #0xe8]
    ldp     x23, x24, [x1, #0xf8]
    ldp     x25, x26, [x1, #0x108]
    ldp     x27, x28, [x1, #0x118]
    ldp     x29, x30, [x1, #0x128]

    // Load target PC into X16
    ldr     x16, [x1, #0x30]

    // Mark new_tcb->state as Running (1) at offset 0x10
    mov     w2, #1
    str     w2, [x1, #0x10]

    // Jump to incoming thread target PC
    br      x16
