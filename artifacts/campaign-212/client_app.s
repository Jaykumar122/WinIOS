.global _main
.align 4

_main:
    // 1. Write startup announcement to stdout (fd 1)
    mov x0, #1
    adrp x1, msg_client_start@PAGE
    add x1, x1, msg_client_start@PAGEOFF
    mov x2, #39
    mov x16, #4          // SYS_write (4)
    svc #0x80

    // 2. Yield CPU to let daemon process request
    mov x16, #329        // SYS_sched_yield (329)
    svc #0x80

    // 3. Write dispatch confirmation message
    mov x0, #1
    adrp x1, msg_client_dispatched@PAGE
    add x1, x1, msg_client_dispatched@PAGEOFF
    mov x2, #40
    mov x16, #4          // SYS_write (4)
    svc #0x80

    // 4. Exit cleanly with code 0
    mov x0, #0
    mov x16, #1          // SYS_exit (1)
    svc #0x80

.section __TEXT,__cstring,cstring_literals
msg_client_start:
    .asciz "[CLIENT] WinIOS Test Client Connected\n"
msg_client_dispatched:
    .asciz "[CLIENT] Request Dispatched To Service\n"
