// ============================================================================
// Guest Kernel SMP Threads Workload
// Architecture: ARM64 Freestanding EL1
// ============================================================================

    .text
    .globl _start
    .p2align 2

_start:
    // Initialize stack
    mov x0, #0x70000
    mov sp, x0

    // Shared structure at 0x20000:
    // [0x20000]: Spinlock (32-bit: 0=unlocked, 1=locked)
    // [0x20004]: Owner TID
    // [0x20008]: Shared Counter (64-bit)
    mov x20, #0x20000
    str wzr, [x20]          // Lock = 0
    str wzr, [x20, #4]      // Owner = 0
    str xzr, [x20, #8]      // Counter = 0

    // Thread 10 loop: 10 iterations
    mov x21, #10            // Iteration count
    mov x22, #10            // TID = 10

.Lloop_t1:
    cbz x21, .Ldone_t1

    // Acquire spinlock
.Lspin_acquire:
    ldaxr w2, [x20]
    cbnz w2, .Lspin_acquire
    mov w3, #1
    stlxr w4, w3, [x20]
    cbnz w4, .Lspin_acquire

    // In critical section: store TID, increment counter
    str w22, [x20, #4]
    ldr x5, [x20, #8]
    add x5, x5, #1
    str x5, [x20, #8]

    // Clear TID, release spinlock
    str wzr, [x20, #4]
    stlr wzr, [x20]

    sub x21, x21, #1
    b .Lloop_t1

.Ldone_t1:
    // Verify counter at 0x20008 == 10
    ldr x1, [x20, #8]
    mov x2, #0x40000
    str x1, [x2]            // [0x40000] = 10

    // Write signature 0x534D5054 ("SMPT") at 0x40008
    movz x3, #0x5054
    movk x3, #0x534D, lsl #16
    str x3, [x2, #8]

.Lidle:
    wfi
    b .Lidle
