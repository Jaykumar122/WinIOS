.text
.align 4
.globl _start
.globl start

_start:
start:
    // Initialize frame pointer and link register to 0
    mov x29, #0
    mov x30, #0

    // Save initial X0 (pointer to GuestAppArgBlock) in callee-saved X19
    mov x19, x0

    // Initialize runtime with ArgBlock pointer
    bl _winios_runtime_init

    // Get argc into W0 and argv into X1
    bl _winios_get_argc
    mov w20, w0       // Save argc in W20

    bl _winios_get_argv
    mov x1, x0        // Argv in X1
    mov w0, w20       // Argc in W0

    // Call main(argc, argv)
    bl _main

    // Call winios_exit with main's return code in W0
    bl _winios_exit

    // If winios_exit ever returns, invoke SYS_exit directly
    mov x16, #1       // SYS_exit
    svc #0x80

1:
    wfe
    b 1b
