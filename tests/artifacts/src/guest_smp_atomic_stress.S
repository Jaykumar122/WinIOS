// ============================================================================
// Guest SMP Atomic Stress Workload
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
    // [0x20000]: Shared counter (LDADD destination)
    // [0x20008]: SWP test target
    // [0x20010]: CAS test target
    // [0x20018]: Loop iterations (50)
    // [0x40000]: Result signature

    mov x19, #0x20000
    str xzr, [x19]          // Counter = 0
    str xzr, [x19, #8]      // SWP target = 0
    str xzr, [x19, #16]     // CAS target = 0

    // 1. LDADD Loop (50 iterations)
    mov x20, #50
    mov x1, #1
.Lldadd_loop:
    ldadd x1, x2, [x19]
    sub x20, x20, #1
    cbnz x20, .Lldadd_loop

    // 2. SWP Test
    add x14, x19, #8
    mov x3, #0x1234
    swp x3, x4, [x14]

    // 3. CAS Test
    add x15, x19, #16
    mov x5, #0              // Expected: 0
    mov x6, #0x5678         // Desired: 0x5678
    cas x5, x6, [x15]

    // Verify results
    ldr x7, [x19]           // Should be 50
    ldr x8, [x19, #8]       // Should be 0x1234
    ldr x9, [x19, #16]      // Should be 0x5678

    cmp x7, #50
    b.ne .Lfail
    mov x10, #0x1234
    cmp x8, x10
    b.ne .Lfail
    mov x11, #0x5678
    cmp x9, x11
    b.ne .Lfail

    // Write success signature 0x41544F4D ("ATOM")
    mov x12, #0x40000
    movz x13, #0x4F4D
    movk x13, #0x4154, lsl #16
    str x13, [x12]
    hlt #0

.Lfail:
    mov x12, #0x40000
    movz x13, #0xDEAD
    str x13, [x12]
    hlt #0
