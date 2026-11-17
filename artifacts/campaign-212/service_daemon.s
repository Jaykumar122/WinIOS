.global _main
.align 4

_main:
    // 1. Write startup announcement to stdout (fd 1)
    mov x0, #1
    adrp x1, msg_daemon_start@PAGE
    add x1, x1, msg_daemon_start@PAGEOFF
    mov x2, #42
    mov x16, #4          // SYS_write (4)
    svc #0x80

    // 2. Yield CPU to allow other processes to schedule
    mov x16, #329        // SYS_sched_yield (329)
    svc #0x80

    // 3. Write active processing message
    mov x0, #1
    adrp x1, msg_daemon_active@PAGE
    add x1, x1, msg_daemon_active@PAGEOFF
    mov x2, #38
    mov x16, #4          // SYS_write (4)
    svc #0x80

    // 4. Exit cleanly with code 0
    mov x0, #0
    mov x16, #1          // SYS_exit (1)
    svc #0x80

.section __TEXT,__cstring,cstring_literals
msg_daemon_start:
    .asciz "[DAEMON] WinIOS Background Service Active\n"
msg_daemon_active:
    .asciz "[DAEMON] Service Processing Requests\n"
