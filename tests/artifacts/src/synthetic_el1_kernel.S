// ============================================================================
// Synthetic EL1 Kernel Runtime & EL0 Exception Client
// Architecture: ARM64 (AArch64) Freestanding
// Target: WinIOS Campaign 193 Synthetic Kernel Runtime
// ============================================================================

    .text
    .globl _start
    .p2align 11

// Vector Table Base (must be aligned to 2048 bytes / 2KB)
vector_table:
    // --- Current EL with SP0 (0x000 - 0x180) ---
    .p2align 7
curr_sp0_sync:
    b .Lhang_vec
    .p2align 7
curr_sp0_irq:
    b .Lhang_vec
    .p2align 7
curr_sp0_fiq:
    b .Lhang_vec
    .p2align 7
curr_sp0_serror:
    b .Lhang_vec

    // --- Current EL with SPx (0x200 - 0x380) ---
    .p2align 7
curr_spx_sync:
    // Nested sync exception handler
    mov x10, #0x40000
    ldr x11, [x10, #48]     // 0x40030: nested_sync_count
    add x11, x11, #1
    str x11, [x10, #48]
    eret

    .p2align 7
curr_spx_irq:
    // Nested IRQ handler
    mov x10, #0x40000
    ldr x11, [x10, #56]     // 0x40038: nested_irq_count
    add x11, x11, #1
    str x11, [x10, #56]
    eret

    .p2align 7
curr_spx_fiq:
    b .Lhang_vec
    .p2align 7
curr_spx_serror:
    b .Lhang_vec

    // --- Lower EL using AArch64 (0x400 - 0x580) ---
    .p2align 7
lower_a64_sync:
    mrs x9, esr_el1
    lsr x10, x9, #26        // Exception Class (EC)
    cmp x10, #0x15          // EC_AA64_SVC = 0x15
    b.ne .Lhandle_data_abort

    // Syscall dispatcher based on x8
    cmp x8, #1              // SYS_diag_print
    b.ne .Lcheck_sys2
    // x0 = string ptr, x1 = length, destination = 0x90000 (virtual console)
    mov x11, #0x90000
.Lprint_loop:
    cbz x1, .Lprint_done
    ldrb w12, [x0], #1
    strb w12, [x11], #1
    sub x1, x1, #1
    b .Lprint_loop
.Lprint_done:
    mov x0, #0
    eret

.Lcheck_sys2:
    cmp x8, #2              // SYS_incr_counter
    b.ne .Lcheck_sys3
    mov x11, #0x40000       // EL1 state base address
    ldr x12, [x11]          // 0x40000: counter
    add x12, x12, #1
    str x12, [x11]
    mov x0, x12             // Return updated count in x0
    eret

.Lcheck_sys3:
    cmp x8, #3              // SYS_exit
    b.ne .Lunknown_svc
    mov x11, #0x40000
    mov x12, #1
    str x12, [x11, #8]      // 0x40008: completion_flag
    mov x0, #42             // Status code 42
    eret

.Lunknown_svc:
    mov x0, #-1
    eret

.Lhandle_data_abort:
    cmp x10, #0x24          // EC_AA64_DATA_ABORT_LOWER = 0x24
    b.ne .Lhang_vec
    // Page fault handler: record FAR_EL1 and increment fault counter
    mrs x11, far_el1
    mov x12, #0x40000
    str x11, [x12, #16]     // 0x40010: fault_addr
    ldr x13, [x12, #24]     // 0x40018: fault_counter
    add x13, x13, #1
    str x13, [x12, #24]
    // Advance ELR_EL1 past faulting instruction for synthetic resolution
    mrs x14, elr_el1
    add x14, x14, #4
    msr elr_el1, x14
    eret

    .p2align 7
lower_a64_irq:
    // Interrupt handler
    mrs x9, s3_0_c12_c12_0  // MRS X9, ICC_IAR1_EL1
    mov x10, #0x40000
    str x9, [x10, #32]      // 0x40020: irq_intid
    ldr x11, [x10, #40]     // 0x40028: irq_count
    add x11, x11, #1
    str x11, [x10, #40]

    // If virtual timer (INTID 27), mask timer
    cmp x9, #27
    b.ne .Lirq_eoi
    mov x12, #2             // IMASK = 1
    msr cntv_ctl_el0, x12

.Lirq_eoi:
    msr s3_0_c12_c12_1, x9  // MSR ICC_EOIR1_EL1, X9
    eret

    .p2align 7
lower_a64_fiq:
    b .Lhang_vec
    .p2align 7
lower_a64_serror:
    b .Lhang_vec

    // --- Lower EL using AArch32 (0x600 - 0x780) ---
    .p2align 7
lower_a32_sync:
    b .Lhang_vec
    .p2align 7
lower_a32_irq:
    b .Lhang_vec
    .p2align 7
lower_a32_fiq:
    b .Lhang_vec
    .p2align 7
lower_a32_serror:
    b .Lhang_vec

.Lhang_vec:
    b .Lhang_vec

// ============================================================================
// Kernel Initialization & EL0 Transition
// ============================================================================
    .p2align 2
_start:
    // 1. Program VBAR_EL1 to vector_table
    adr x0, vector_table
    msr vbar_el1, x0

    // 2. Setup SP for EL1 and SP_EL0 for user mode
    mov x0, #0x70000
    mov sp, x0
    mov x0, #0x80000
    msr sp_el0, x0

    // 3. Clear kernel memory structures at 0x40000..0x40040
    mov x1, #0x40000
    str xzr, [x1]           // counter = 0 (0x40000)
    str xzr, [x1, #8]       // completion_flag = 0 (0x40008)
    str xzr, [x1, #16]      // fault_addr = 0 (0x40010)
    str xzr, [x1, #24]      // fault_count = 0 (0x40018)
    str xzr, [x1, #32]      // irq_intid = 0 (0x40020)
    str xzr, [x1, #40]      // irq_count = 0 (0x40028)
    str xzr, [x1, #48]      // nested_count = 0 (0x40030)
    str xzr, [x1, #56]      // nested_irq = 0 (0x40038)

    // 4. Configure return address to _client_entry at EL0
    adr x2, _client_entry
    msr elr_el1, x2

    // 5. Configure SPSR_EL1 = 0 (EL0t, unmasked DAIF)
    mov x3, #0
    msr spsr_el1, x3

    // 6. Transition privilege from EL1 to EL0 via ERET!
    eret

// ============================================================================
// EL0 Client Program
// ============================================================================
    .p2align 2
_client_entry:
    // Step 1: Request diagnostic print via SVC
    adr x0, diag_string
    mov x1, #19             // length of "SyntheticKernel-OK\n"
    mov x8, #1              // SYS_diag_print
    svc #0

    // Step 2: Request counter increment via SVC
    mov x8, #2              // SYS_incr_counter
    svc #0
    // Result returned in x0 (should be 1)

    // Second counter increment
    mov x8, #2
    svc #0
    // Result returned in x0 (should be 2)

    // Step 3: Exit/halt client
    mov x8, #3              // SYS_exit
    svc #0
    // Result returned in x0 (should be 42)

.Lclient_idle:
    b .Lclient_idle

// ============================================================================
// Read-Only Data
// ============================================================================
    .p2align 2
diag_string:
    .ascii "SyntheticKernel-OK\n"
