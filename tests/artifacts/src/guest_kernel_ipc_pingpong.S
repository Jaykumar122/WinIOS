// ============================================================================
// Guest Kernel IPC Pingpong Workload
// Architecture: ARM64 Freestanding EL1
// ============================================================================

    .text
    .globl _start
    .p2align 2

_start:
    // Initialize stack
    mov x0, #0x70000
    mov sp, x0

    // Mailbox 1 (A->B) at 0x20000:
    // [0x20000]: Flag (0=empty, 1=ready)
    // [0x20004]: MsgID
    // [0x20008]: Value
    mov x20, #0x20000
    str wzr, [x20]
    str wzr, [x20, #4]
    str xzr, [x20, #8]

    // Mailbox 2 (B->A) at 0x20020:
    mov x21, #0x20000
    add x21, x21, #0x20
    str wzr, [x21]
    str wzr, [x21, #4]
    str xzr, [x21, #8]

    // 5 rounds
    mov x22, #5             // Round counter
    mov x23, #0             // Cumulative payload counter

.Lping_round:
    cbz x22, .Lping_done

    // Task A: Send message to Mailbox 1
    mov w2, #1
    str w2, [x20, #4]       // MsgID = 1
    str x23, [x20, #8]      // Value
    str w2, [x20]           // Flag = 1 (Ready)

    // Task B: Receive from Mailbox 1
.Lwait_mb1:
    ldr w3, [x20]
    cbz w3, .Lwait_mb1
    ldr x4, [x20, #8]
    add x4, x4, #1          // Increment payload value
    str wzr, [x20]          // Clear Flag in MB1

    // Task B: Reply to Mailbox 2
    mov w5, #2
    str w5, [x21, #4]       // MsgID = 2
    str x4, [x21, #8]       // Value = x4
    str w5, [x21]           // Flag = 1

    // Task A: Receive reply from Mailbox 2
.Lwait_mb2:
    ldr w6, [x21]
    cbz w6, .Lwait_mb2
    ldr x23, [x21, #8]      // x23 updated
    str wzr, [x21]          // Clear Flag in MB2

    sub x22, x22, #1
    b .Lping_round

.Lping_done:
    // Store final payload value at 0x40000 (expected: 5)
    mov x10, #0x40000
    str x23, [x10]

    // Write signature 0x50494E47 ("PING") at 0x40008
    movz x11, #0x4E47
    movk x11, #0x5049, lsl #16
    str x11, [x10, #8]

.Lidle:
    wfi
    b .Lidle
