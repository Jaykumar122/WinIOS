.text
.align 4
.globl __start
.globl _start
.globl start_first_cpu
.globl secondary_park

__start:
_start:
    // 1. Mask all architectural interrupts (DAIFSet, #0xF)
    msr daifset, #0xf

    // 2. Query core affinity via MPIDR_EL1
    mrs x20, mpidr_el1
    and x20, x20, #0xff
    cbnz x20, secondary_park

start_first_cpu:
    // 3. Enable FP/SIMD access (CPACR_EL1.FPEN = 0b11)
    mov x2, #0x300000
    msr cpacr_el1, x2
    isb

    // 4. Establish bootstrap stack pointer (SP_EL1)
    adrp x1, bootstrap_stack_top@PAGE
    add x1, x1, bootstrap_stack_top@PAGEOFF
    mov sp, x1

    // 5. Preserve boot_args pointer in callee-saved register X19
    mov x19, x0

    // 6. Call C language arm_init(boot_args *args)
    mov x0, x19
    bl _arm_init

    // 7. On return from arm_init, halt cleanly via WFI
    wfi
1:
    b 1b

secondary_park:
    wfe
    b secondary_park

.data
.align 14
.globl bootstrap_stack
.globl bootstrap_stack_top
bootstrap_stack:
    .space 16384
bootstrap_stack_top:
