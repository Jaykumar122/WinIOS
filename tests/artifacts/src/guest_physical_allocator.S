// ============================================================================
// Guest Physical Memory Manager (16KB Page Allocator)
// Architecture: ARM64 Freestanding EL1
// ============================================================================

    .text
    .globl _start
    .p2align 2

_start:
    // Initialize stack pointer
    mov x0, #0x70000
    mov sp, x0

    // Setup bitmap parameters:
    // Bitmap buffer at 0x20000 (32 bytes = 256 pages of 16KB = 4MB)
    // Clear bitmap first
    mov x0, #0x20000
    mov x1, #32
.Lclear_loop:
    cbz x1, .Lalloc_test
    strb wzr, [x0], #1
    sub x1, x1, #1
    b .Lclear_loop

.Lalloc_test:
    // 1. First allocation: palloc_16k(0x20000, 256)
    mov x0, #0x20000
    mov x1, #256
    bl palloc_16k
    // Store returned page index at 0x40000
    mov x10, #0x40000
    str x0, [x10]           // expected: 0

    // 2. Second allocation: palloc_16k(0x20000, 256)
    mov x0, #0x20000
    mov x1, #256
    bl palloc_16k
    str x0, [x10, #8]       // expected: 1

    // 3. Third allocation: palloc_16k(0x20000, 256)
    mov x0, #0x20000
    mov x1, #256
    bl palloc_16k
    str x0, [x10, #16]      // expected: 2

    // 4. Free page 1: pfree_16k(0x20000, 1)
    mov x0, #0x20000
    mov x1, #1
    bl pfree_16k
    str x0, [x10, #24]      // expected: 0 (success)

    // 5. Re-allocate: should reuse page 1!
    mov x0, #0x20000
    mov x1, #256
    bl palloc_16k
    str x0, [x10, #32]      // expected: 1

    // 6. Set completion flag at 0x40028
    mov x11, #1
    str x11, [x10, #40]

    // Exit code 77 in x0
    mov x0, #77
.Lidle:
    b .Lidle

// ----------------------------------------------------------------------------
// palloc_16k: Allocates one page from bitmap
// Input:  x0 = bitmap base address
//         x1 = total pages
// Output: x0 = allocated page index (or -1 if full)
// ----------------------------------------------------------------------------
palloc_16k:
    mov x2, #0              // byte_idx = 0
.Lbyte_loop:
    lsl x3, x2, #3          // bit_offset = byte_idx * 8
    cmp x3, x1
    b.ge .Lalloc_fail
    ldrb w4, [x0, x2]       // load byte from bitmap
    cmp w4, #0xFF
    b.ne .Lbit_search
    add x2, x2, #1
    b .Lbyte_loop

.Lbit_search:
    mov x5, #0              // bit_idx = 0
.Lbit_loop:
    mov w6, #1
    lsl w6, w6, w5          // mask = 1 << bit_idx
    tst w4, w6
    b.eq .Lbit_found
    add x5, x5, #1
    cmp x5, #8
    b.lt .Lbit_loop
    b .Lalloc_fail

.Lbit_found:
    orr w4, w4, w6          // set bit in byte
    strb w4, [x0, x2]       // store updated byte in RAM
    lsl x2, x2, #3
    add x0, x2, x5          // page_idx = byte_idx * 8 + bit_idx
    ret

.Lalloc_fail:
    mov x0, #-1
    ret

// ----------------------------------------------------------------------------
// pfree_16k: Frees page index in bitmap
// Input:  x0 = bitmap base address
//         x1 = page index to free
// Output: x0 = 0
// ----------------------------------------------------------------------------
pfree_16k:
    lsr x2, x1, #3          // byte_idx = page_idx / 8
    and x3, x1, #7          // bit_idx = page_idx % 8
    ldrb w4, [x0, x2]
    mov w5, #1
    lsl w5, w5, w3
    bic w4, w4, w5
    strb w4, [x0, x2]
    mov x0, #0
    ret
