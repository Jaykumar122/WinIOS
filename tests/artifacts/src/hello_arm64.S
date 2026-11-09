.globl _start
.p2align 2

_start:
    // Darwin ARM64 System Call ABI:
    // SYS_write = 4 (X16 = 4, X0 = fd (1), X1 = buffer, X2 = count (23))
    mov x0, #1
    adrp x1, msg@PAGE
    add x1, x1, msg@PAGEOFF
    mov x2, #23
    mov x16, #4
    svc #0x80

    // SYS_exit = 1 (X16 = 1, X0 = status (0))
    mov x0, #0
    mov x16, #1
    svc #0x80

.data
.p2align 2
msg:
    .ascii "Hello from real Mach-O\n"
