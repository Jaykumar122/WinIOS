// ==============================================================================
// WinIOS Campaign 199 Stage 18: Guest Stage-2 MMU, IOMMU & PAC Workload
// ==============================================================================
// Freestanding ARM64e Mach-O workload exercising:
// 1. ARM64e PACIA / AUTIA instruction execution
// 2. ARM64e PACGA (generic authentication tag generation)
// 3. ARM64e PACIZA / AUTIZA (zero modifier authentication)
// 4. Memory read/write integrity at translated regions (Stage-2 / IOMMU)
// 5. Completion signature signaling in memory mailbox
// ==============================================================================

.text
.globl _start
.p2align 4

_start:
    // Setup initial frame
    mov     x29, #0
    mov     x30, #0

    // Mailbox address: 0x40000
    movz    x2, #0x0000
    movk    x2, #0x0004, lsl #16

    // Translated workspace buffer address: 0x50000
    movz    x3, #0x0000
    movk    x3, #0x0005, lsl #16

    // --- 1. Test PACIA / AUTIA ---
    // Load test pointer 0x10008000 into x0
    movz    x0, #0x8000
    movk    x0, #0x1000, lsl #16
    // Load modifier 0x12345678 into x1
    movz    x1, #0x5678
    movk    x1, #0x1234, lsl #16

    // Sign pointer with IA key
    pacia   x0, x1
    // Store signed pointer to mailbox [x2, #8]
    str     x0, [x2, #8]

    // Authenticate pointer with IA key
    autia   x0, x1

    // Verify pointer is restored to 0x10008000
    movz    x4, #0x8000
    movk    x4, #0x1000, lsl #16
    cmp     x0, x4
    b.ne    .L_fail

    // --- 2. Test PACGA ---
    // Generate MAC tag from x0 (0x10008000) and x1 (0x12345678)
    pacga   x5, x0, x1
    // Store generated generic tag to mailbox [x2, #16]
    str     x5, [x2, #16]
    // Verify tag is non-zero
    cbz     x5, .L_fail

    // --- 3. Test PACIZA / AUTIZA ---
    // Load test pointer 0x20004000 into x6
    movz    x6, #0x4000
    movk    x6, #0x2000, lsl #16
    paciza  x6
    // Store signed pointer to mailbox [x2, #24]
    str     x6, [x2, #24]
    autiza  x6

    // Verify restored pointer
    movz    x7, #0x4000
    movk    x7, #0x2000, lsl #16
    cmp     x6, x7
    b.ne    .L_fail

    // --- 4. Test Memory Store & Load through Translated Buffer ---
    // Store test pattern into [x3] (0x50000)
    movz    x8, #0x1991
    movk    x8, #0x1990, lsl #16
    str     x8, [x3]

    // Load back and verify
    ldr     x9, [x3]
    cmp     x8, x9
    b.ne    .L_fail

    // Also write secondary marker at [x3, #8]
    movz    x10, #0xCAFE
    movk    x10, #0xBABE, lsl #16
    str     x10, [x3, #8]

    // --- 5. Signal Completion Signature ---
    // Success signature: 0x1991C0DE
    movz    x11, #0xC0DE
    movk    x11, #0x1991, lsl #16
    str     x11, [x2]

.L_done:
    b       .L_done

.L_fail:
    // Failure signature: 0xDEAD1990
    movz    x12, #0x1990
    movk    x12, #0xDEAD, lsl #16
    str     x12, [x2]

.L_spin:
    b       .L_spin
