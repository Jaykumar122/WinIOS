.globl _winios_write
.p2align 2
_winios_write:
    mov x16, #4
    svc #0x80
    ret

.globl _winios_exit
.p2align 2
_winios_exit:
    mov x16, #1
    svc #0x80
    ret

.globl _winios_getpid
.p2align 2
_winios_getpid:
    mov x16, #20
    svc #0x80
    ret

.globl _winios_getppid
.p2align 2
_winios_getppid:
    mov x16, #39
    svc #0x80
    ret

.globl _winios_yield
.p2align 2
_winios_yield:
    mov x16, #329
    svc #0x80
    ret

.globl _winios_strlen
.p2align 2
_winios_strlen:
    mov x1, x0
1:
    ldrb w2, [x1], #1
    cbnz w2, 1b
    sub x0, x1, x0
    sub x0, x0, #1
    ret

.globl _winios_print
.p2align 2
_winios_print:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x19, x0
    bl _winios_strlen
    mov x2, x0
    mov x1, x19
    mov x0, #1
    bl _winios_write
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    ret
