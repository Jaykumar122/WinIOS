// ============================================================================
// Guest Kernel Page Walk Workload
// Architecture: ARM64 Freestanding EL1 (16KB granule translation)
// ============================================================================

    .text
    .globl _start
    .p2align 2

_start:
    // Initialize stack
    mov x0, #0x70000
    mov sp, x0

    // Setup L2 table at 0x20000
    // L2[0] points to L3 table at 0x24000 (Table descriptor = Base | 0x3)
    mov x10, #0x20000
    movz x11, #0x4003
    movk x11, #2, lsl #16   // x11 = 0x24003
    str x11, [x10]

    // Setup L3 table at 0x24000
    // For VA 0x40000:
    // L3 index = (0x40000 >> 14) & 0x7FF = 16
    // Offset in table = 16 * 8 = 128 (0x80)
    // L3[16] points to PA 0x60000 (Page descriptor = PA | 0x3)
    mov x12, #0x20000
    add x12, x12, #0x4000   // x12 = 0x24000
    movz x13, #3
    movk x13, #6, lsl #16   // x13 = 0x60003
    str x13, [x12, #128]

    // Perform architectural 16KB Page Walk for VA 0x40000:
    mov x0, #0x40000        // VA
    mov x1, #0x20000        // L2 base

    // 1. L2 Index: bits [35:25] (11 bits)
    ubfx x2, x0, #25, #11
    ldr x3, [x1, x2, lsl #3] // L2 descriptor

    // Check valid table descriptor (bits [1:0] == 0b11)
    and x4, x3, #3
    cmp x4, #3
    b.ne .Lwalk_fail

    // Mask table address (16KB aligned: bits [47:14])
    mov x4, #0x3FFF
    bic x5, x3, x4          // x5 = L3 table base (0x24000)

    // 2. L3 Index: bits [24:14] (11 bits)
    ubfx x6, x0, #14, #11
    ldr x7, [x5, x6, lsl #3] // L3 descriptor

    // Check valid page descriptor (bits [1:0] == 0b11)
    and x8, x7, #3
    cmp x8, #3
    b.ne .Lwalk_fail

    // Mask physical page address
    bic x9, x7, x4          // x9 = 0x60000
    // Add page offset (bits [13:0])
    and x14, x0, x4
    orr x15, x9, x14        // x15 = Translated Physical Address (0x60000)

    // Store result at 0x40000
    mov x16, #0x40000
    str x15, [x16]

    // Write signature 0x5047574B ("PGWK") at 0x40008
    movz x17, #0x574B
    movk x17, #0x5047, lsl #16
    str x17, [x16, #8]
    b .Lidle

.Lwalk_fail:
    mov x16, #0x40000
    mov x17, #-1
    str x17, [x16]

.Lidle:
    wfi
    b .Lidle
