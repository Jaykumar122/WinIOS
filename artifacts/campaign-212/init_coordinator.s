.global _main
.align 4

_main:
    // 1. Write startup announcement to stdout (fd 1)
    mov x0, #1
    adrp x1, msg_init_start@PAGE
    add x1, x1, msg_init_start@PAGEOFF
    mov x2, #48
    mov x16, #4          // SYS_write (4)
    svc #0x80

    // 2. Initial yield to give children CPU time
    mov x16, #329        // SYS_sched_yield (329)
    svc #0x80

wait_loop:
    // 3. Poll/reap child processes with wait4(-1, NULL, WNOHANG(1), NULL)
    mov x0, #-1
    mov x1, #0
    mov x2, #1          // WNOHANG = 1
    mov x3, #0
    mov x16, #7         // SYS_wait4 (7)
    svc #0x80

    // In Darwin ABI, Carry Set (b.cs) indicates error (e.g. ECHILD: all children reaped)
    b.cs wait_done

    // Carry clear: success. Check return value in X0
    cmp x0, #0
    b.gt wait_loop      // Child reaped (>0), reap next

    // X0 == 0: children still running -> yield CPU and retry
    b do_yield

do_yield:
    mov x16, #329       // SYS_sched_yield (329)
    svc #0x80
    b wait_loop

wait_done:
    // 4. Write completion message
    mov x0, #1
    adrp x1, msg_init_done@PAGE
    add x1, x1, msg_init_done@PAGEOFF
    mov x2, #43
    mov x16, #4         // SYS_write (4)
    svc #0x80

    // 5. Exit cleanly with code 0
    mov x0, #0
    mov x16, #1         // SYS_exit (1)
    svc #0x80

.section __TEXT,__cstring,cstring_literals
msg_init_start:
    .asciz "[INIT] WinIOS Multi-Process Coordinator Started\n"
msg_init_done:
    .asciz "[INIT] All child services reaped. Exiting.\n"
