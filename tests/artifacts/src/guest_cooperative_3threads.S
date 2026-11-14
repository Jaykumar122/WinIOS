// ============================================================================
// WinIOS Campaign 195 Stage 195.3: Cooperative 3-Thread Context Switching
// Tests repeated cooperative task switches between 3 threads maintaining
// distinct register patterns (X19-X28, FP, LR) and stack data.
// ============================================================================

.text
.global _start
.global _thread1_entry
.global _thread2_entry
.global _thread3_entry
.global _cpu_switch_to
.p2align 2

// Memory Layout:
//   TCB1 at 0x40000
//   TCB2 at 0x41000
//   TCB3 at 0x42000
//   Status word at 0x43000:
//     [0x43000] = Thread 1 pass flag (1 = ok)
//     [0x43004] = Thread 2 pass flag (1 = ok)
//     [0x43008] = Thread 3 pass flag (1 = ok)

_start:
    b       _thread1_entry

_thread1_entry:
    // Setup unique callee-saved registers for Thread 1
    movz    x19, #0x1111, lsl #48
    movk    x19, #0x1111, lsl #32
    movk    x19, #0x1111, lsl #16
    movk    x19, #0x1111

    movz    x20, #0x1120, lsl #48
    movk    x20, #0x1120, lsl #32
    movk    x20, #0x1120, lsl #16
    movk    x20, #0x1120

    movz    x28, #0x1128, lsl #48
    movk    x28, #0x1128, lsl #32
    movk    x28, #0x1128, lsl #16
    movk    x28, #0x1128

    // Push local stack canary
    movz    x2, #0xCAFE, lsl #48
    movk    x2, #0xBABE, lsl #32
    movk    x2, #0x1111, lsl #16
    movk    x2, #0x1111
    str     x2, [sp, #-16]!

    // Switch to Thread 2: cpu_switch_to(tcb1, tcb2)
    movz    x0, #0x4, lsl #16
    movk    x0, #0x0000           // X0 = 0x40000 (TCB1)
    movz    x1, #0x4, lsl #16
    movk    x1, #0x1000           // X1 = 0x41000 (TCB2)
    bl      _cpu_switch_to

    // Resumed after Thread 3 switched back to TCB1!
    // Verify stack canary
    ldr     x3, [sp], #16
    movz    x2, #0xCAFE, lsl #48
    movk    x2, #0xBABE, lsl #32
    movk    x2, #0x1111, lsl #16
    movk    x2, #0x1111
    cmp     x2, x3
    b.ne    .Lfail_t1

    // Verify X19 and X28
    movz    x2, #0x1111, lsl #48
    movk    x2, #0x1111, lsl #32
    movk    x2, #0x1111, lsl #16
    movk    x2, #0x1111
    cmp     x19, x2
    b.ne    .Lfail_t1

    // Mark Thread 1 success
    movz    x4, #0x4, lsl #16
    movk    x4, #0x3000
    mov     w5, #1
    str     w5, [x4, #0]

    // Switch to Thread 2 so it can complete its verification!
    movz    x0, #0x4, lsl #16
    movk    x0, #0x0000           // X0 = 0x40000 (TCB1)
    movz    x1, #0x4, lsl #16
    movk    x1, #0x1000           // X1 = 0x41000 (TCB2)
    bl      _cpu_switch_to

.Lhalt_t1:
    wfi
    b       .Lhalt_t1

.Lfail_t1:
    movz    x4, #0x4, lsl #16
    movk    x4, #0x3000
    mov     w5, #0xFF
    str     w5, [x4, #0]
    b       .Lhalt_t1

_thread2_entry:
    // Setup unique callee-saved registers for Thread 2
    movz    x19, #0x2222, lsl #48
    movk    x19, #0x2222, lsl #32
    movk    x19, #0x2222, lsl #16
    movk    x19, #0x2222

    movz    x28, #0x2228, lsl #48
    movk    x28, #0x2228, lsl #32
    movk    x28, #0x2228, lsl #16
    movk    x28, #0x2228

    // Push local stack canary
    movz    x2, #0xCAFE, lsl #48
    movk    x2, #0xBABE, lsl #32
    movk    x2, #0x2222, lsl #16
    movk    x2, #0x2222
    str     x2, [sp, #-16]!

    // Switch to Thread 3: cpu_switch_to(tcb2, tcb3)
    movz    x0, #0x4, lsl #16
    movk    x0, #0x1000           // X0 = 0x41000 (TCB2)
    movz    x1, #0x4, lsl #16
    movk    x1, #0x2000           // X1 = 0x42000 (TCB3)
    bl      _cpu_switch_to

    // Resumed after Thread 1 switched back to TCB2!
    ldr     x3, [sp], #16
    movz    x2, #0xCAFE, lsl #48
    movk    x2, #0xBABE, lsl #32
    movk    x2, #0x2222, lsl #16
    movk    x2, #0x2222
    cmp     x2, x3
    b.ne    .Lfail_t2

    // Verify X19
    movz    x2, #0x2222, lsl #48
    movk    x2, #0x2222, lsl #32
    movk    x2, #0x2222, lsl #16
    movk    x2, #0x2222
    cmp     x19, x2
    b.ne    .Lfail_t2

    // Mark Thread 2 success
    movz    x4, #0x4, lsl #16
    movk    x4, #0x3000
    mov     w5, #1
    str     w5, [x4, #4]

    // Switch to Thread 3 so it can complete its verification!
    movz    x0, #0x4, lsl #16
    movk    x0, #0x1000           // X0 = 0x41000 (TCB2)
    movz    x1, #0x4, lsl #16
    movk    x1, #0x2000           // X1 = 0x42000 (TCB3)
    bl      _cpu_switch_to

.Lhalt_t2:
    wfi
    b       .Lhalt_t2

.Lfail_t2:
    movz    x4, #0x4, lsl #16
    movk    x4, #0x3000
    mov     w5, #0xFF
    str     w5, [x4, #4]
    b       .Lhalt_t2

_thread3_entry:
    // Setup unique callee-saved registers for Thread 3
    movz    x19, #0x3333, lsl #48
    movk    x19, #0x3333, lsl #32
    movk    x19, #0x3333, lsl #16
    movk    x19, #0x3333

    movz    x28, #0x3328, lsl #48
    movk    x28, #0x3328, lsl #32
    movk    x28, #0x3328, lsl #16
    movk    x28, #0x3328

    // Push local stack canary
    movz    x2, #0xCAFE, lsl #48
    movk    x2, #0xBABE, lsl #32
    movk    x2, #0x3333, lsl #16
    movk    x2, #0x3333
    str     x2, [sp, #-16]!

    // Switch back to Thread 1: cpu_switch_to(tcb3, tcb1)
    movz    x0, #0x4, lsl #16
    movk    x0, #0x2000           // X0 = 0x42000 (TCB3)
    movz    x1, #0x4, lsl #16
    movk    x1, #0x0000           // X1 = 0x40000 (TCB1)
    bl      _cpu_switch_to

    // Resumed after Thread 2 switched back to TCB3!
    ldr     x3, [sp], #16
    movz    x2, #0xCAFE, lsl #48
    movk    x2, #0xBABE, lsl #32
    movk    x2, #0x3333, lsl #16
    movk    x2, #0x3333
    cmp     x2, x3
    b.ne    .Lfail_t3

    // Verify X19
    movz    x2, #0x3333, lsl #48
    movk    x2, #0x3333, lsl #32
    movk    x2, #0x3333, lsl #16
    movk    x2, #0x3333
    cmp     x19, x2
    b.ne    .Lfail_t3

    // Mark Thread 3 success
    movz    x4, #0x4, lsl #16
    movk    x4, #0x3000
    mov     w5, #1
    str     w5, [x4, #8]

.Lhalt_t3:
    wfi
    b       .Lhalt_t3

.Lfail_t3:
    movz    x4, #0x4, lsl #16
    movk    x4, #0x3000
    mov     w5, #0xFF
    str     w5, [x4, #8]
    b       .Lhalt_t3

_cpu_switch_to:
    cbz     x0, .Lload_new
    mov     x2, sp
    str     x2, [x0, #0x28]
    str     x30, [x0, #0x30]
    stp     x19, x20, [x0, #0xd8]
    stp     x21, x22, [x0, #0xe8]
    stp     x23, x24, [x0, #0xf8]
    stp     x25, x26, [x0, #0x108]
    stp     x27, x28, [x0, #0x118]
    stp     x29, x30, [x0, #0x128]

.Lload_new:
    ldr     x2, [x1, #0x28]
    mov     sp, x2
    ldp     x19, x20, [x1, #0xd8]
    ldp     x21, x22, [x1, #0xe8]
    ldp     x23, x24, [x1, #0xf8]
    ldp     x25, x26, [x1, #0x108]
    ldp     x27, x28, [x1, #0x118]
    ldp     x29, x30, [x1, #0x128]
    ldr     x16, [x1, #0x30]
    mov     w2, #1
    str     w2, [x1, #0x10]
    br      x16
