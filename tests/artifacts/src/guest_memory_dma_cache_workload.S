// ==============================================================================
// WinIOS Campaign 198 Stage 17: Guest Memory, DMA & Cache Maintenance Workload
// ==============================================================================
// Freestanding ARM64 Mach-O workload exercising:
// 1. Data cache zeroing (DC ZVA)
// 2. Self-modifying code synchronization (DC CVAU -> DSB ISH -> IC IVAU -> DSB ISH -> ISB)
// 3. Execution of freshly patched code buffer
// 4. LSE atomic memory operations (LDADD, SWP)
// 5. Completion signature signaling
// ==============================================================================

.text
.globl _start
.p2align 4

_start:
    // Setup stack
    mov     x29, #0
    mov     x30, #0

    // Address of target executable RAM scratch buffer: 0x20000
    movz    x1, #0x0000
    movk    x1, #0x0002, lsl #16

    // Address of completion signature mailbox: 0x40000
    movz    x2, #0x0000
    movk    x2, #0x0004, lsl #16

    // Address of DMA buffer: 0x30000
    movz    x3, #0x0000
    movk    x3, #0x0003, lsl #16

    // 1. Clear target code buffer with DC ZVA (64-byte block zeroing)
    dc      zva, x1
    dsb     ish

    // 2. Write self-modifying code patch into [x1]:
    // Instruction 1: movz w0, #0x0198 -> 0x52803300
    // Instruction 2: ret             -> 0xd65f03c0
    movz    w4, #0x3300
    movk    w4, #0x5280, lsl #16
    str     w4, [x1]

    movz    w4, #0x03c0
    movk    w4, #0xd65f, lsl #16
    str     w4, [x1, #4]

    // 3. Architectural cache maintenance sequence for self-modifying code
    dc      cvau, x1
    dsb     ish
    ic      ivau, x1
    dsb     ish
    isb

    // 4. Execute newly patched code buffer
    blr     x1
    // Return value in w0 must be 0x198
    cmp     w0, #0x198
    b.ne    .L_fail

    // 5. Execute LSE atomic operations on completion signature area
    // Write base value 0x1000 into [x2]
    movz    x5, #0x1000
    str     x5, [x2]

    // LDADD: add 0x98 to [x2] -> memory becomes 0x1098
    movz    x6, #0x0098
    ldadd   x6, x7, [x2]

    // Verify old value returned in x7 was 0x1000
    cmp     x7, #0x1000
    b.ne    .L_fail

    // SWP: swap 0x198C0DE into [x2, #8]
    add     x11, x2, #8
    movz    x8, #0xC0DE
    movk    x8, #0x0198, lsl #16
    swp     x8, x9, [x11]

    // Signal overall success signature at [x2]
    // Sig = 0x198517C0DE (Stage 17 Workload Complete)
    movz    x10, #0xC0DE
    movk    x10, #0x1985, lsl #16
    str     x10, [x2]
    dmb     ish

    // Infinite loop waiting or halt
.L_done:
    b       .L_done

.L_fail:
    movz    x10, #0xDEAD
    str     x10, [x2]
.L_spin:
    b       .L_spin
