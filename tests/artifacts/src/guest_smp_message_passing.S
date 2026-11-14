// ============================================================================
// Guest SMP Message Passing Litmus Workload
// Architecture: ARM64 Freestanding EL1
// Target: aarch64-macos / aarch64-ios
// ============================================================================

    .text
    .globl _start
    .p2align 2

_start:
    // Setup stack
    mov x0, #0x80000
    mov sp, x0

    // Memory layout:
    // [0x20000]: Flag (0 = unready, 1 = ready)
    // [0x20008]: Data payload
    // [0x40000]: Result status

    mov x19, #0x20000
    str xzr, [x19]          // Flag = 0
    str xzr, [x19, #8]      // Data = 0

    // Writer writes data payload then publishes flag with STLR
    movz x1, #0xBEEF
    movk x1, #0xDEAD, lsl #16
    str x1, [x19, #8]       // Write data

    mov x2, #1
    stlr x2, [x19]          // Release-store flag = 1

    // Reader consumes flag with LDAR then reads data
    ldar x3, [x19]
    cmp x3, #1
    b.ne .Lfail

    ldr x4, [x19, #8]
    cmp x4, x1
    b.ne .Lfail

    // Write success signature 0x4D534750 ("MSGP")
    mov x12, #0x40000
    movz x13, #0x4750
    movk x13, #0x4D53, lsl #16
    str x13, [x12]
    hlt #0

.Lfail:
    mov x12, #0x40000
    movz x13, #0xDEAD
    str x13, [x12]
    hlt #0
