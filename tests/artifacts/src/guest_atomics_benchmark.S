// ============================================================================
// Guest Atomics Benchmark Workload (Campaign 197)
// Architecture: ARM64 Freestanding EL1
// Target: aarch64-macos / aarch64-ios
// ============================================================================

    .text
    .globl _start
    .p2align 2

_start:
    // 1. Setup stack
    mov x0, #0x80000
    mov sp, x0

    // Memory layout:
    // [0x20000]: Shared counter
    // [0x20008]: Bitmask target
    // [0x40000]: Status / signature
    // [0x40008]: Final counter value

    mov x19, #0x20000
    str xzr, [x19]          // Counter = 0
    str xzr, [x19, #8]      // Mask = 0

    // 2. Barrier Sequence
    dmb ish
    dsb sy
    isb

    // 3. Exclusive Monitor LL/SC Loop (10 iterations, +10 each = 100)
    mov x20, #10
.Lllsc_loop:
    ldxr x1, [x19]
    add x1, x1, #10
    stxr w2, x1, [x19]
    cbnz w2, .Lllsc_loop
    sub x20, x20, #1
    cbnz x20, .Lllsc_loop

    // 4. LSE Atomic Instructions
    // LDADD: add 50 to [0x20000] -> 150
    mov x3, #50
    ldadd x3, x4, [x19]

    // LDSET: set bit 0x8000 in [0x20008]
    add x21, x19, #8
    mov x5, #0x8000
    ldset x5, x6, [x21]

    // LDCLR: clear bit 0x4000 in [0x20008] (no-op since not set, but tests decoding)
    mov x7, #0x4000
    ldclr x7, x8, [x21]

    // LDEOR: toggle bits 0x8001 in [0x20008] -> 0x8000 ^ 0x8001 = 1
    mov x9, #0x8001
    ldeor x9, x10, [x21]

    // SWP: swap with 0xCAFEBABE in [0x20000]
    movz x11, #0xBABE
    movk x11, #0xCAFE, lsl #16
    swp x11, x12, [x19]      // old value (150) returned in x12

    // CAS: compare with 0xCAFEBABE, swap with 0x197C0DE
    mov x13, x11             // expected: 0xCAFEBABE
    movz x14, #0xC0DE
    movk x14, #0x0197, lsl #16 // desired: 0x0197C0DE
    cas x13, x14, [x19]      // succeeds!

    // Verify final state
    ldr x15, [x19]           // Should be 0x0197C0DE
    cmp x15, x14
    b.ne .Lfail

    ldr x16, [x21]           // Should be 1
    cmp x16, #1
    b.ne .Lfail

    // Barriers before publishing completion
    dmb ish
    dsb sy

    // Success signature: 0x197A10CC ("197 Atomics Complete")
    mov x17, #0x40000
    movz x18, #0x10CC
    movk x18, #0x197A, lsl #16
    str x15, [x17, #8]      // write final value first
    str x18, [x17]          // write signature to signal completion
    hlt #0

.Lfail:
    mov x17, #0x40000
    movz x18, #0xDEAD
    str x18, [x17]
    hlt #0
