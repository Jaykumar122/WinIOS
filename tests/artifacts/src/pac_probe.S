.text
.globl _start
.align 2
_start:
    paciasp
    autiasp
    mov x0, #0
    mov x16, #1
    svc #0x80
