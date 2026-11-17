// ============================================================================
// WinIOS Campaign 213: Freestanding ARM64 Interactive Counter App
// Executes genuinely at EL0 under Darwin Mach-O ABI
// ============================================================================
.global _main
.align 4

_main:
    // 1. Announce startup on stdout
    mov x0, #1
    adrp x1, msg_counter_start@PAGE
    add x1, x1, msg_counter_start@PAGEOFF
    mov x2, #32
    mov x16, #4          // SYS_write (4)
    svc #0x80

    // 2. Create Counter window via SYS_winios_window_create (500)
    adrp x0, str_counter_title@PAGE
    add x0, x0, str_counter_title@PAGEOFF
    mov x1, #7           // len("Counter")
    mov x2, #50          // x = 50
    mov x3, #150         // y = 150
    mov x4, #700         // w = 700
    mov x5, #900         // h = 900
    mov x16, #500
    svc #0x80

    mov x19, x0          // X19 = window ID
    mov x20, #0          // X20 = counter value (0)

    // 3. Fill window background with light gray (0xFFF0F0F5)
    mov x0, x19
    mov x1, #0
    mov x2, #0
    mov x3, #700
    mov x4, #900
    movz x5, #0xF0F5
    movk x5, #0xFFF0, lsl #16
    mov x16, #505        // SYS_winios_surface_fill
    svc #0x80

    // 4. Draw "+1 Increment" button at [100, 300, 200, 80] (Blue 0xFF007AFF)
    mov x0, x19
    mov x1, #100
    mov x2, #300
    mov x3, #200
    mov x4, #80
    movz x5, #0x7AFF
    movk x5, #0xFF00, lsl #16
    mov x16, #505
    svc #0x80

    // 5. Draw "Exit" button at [400, 300, 200, 80] (Red 0xFFFF3B30)
    mov x0, x19
    mov x1, #400
    mov x2, #300
    mov x3, #200
    mov x4, #80
    movz x5, #0x3B30
    movk x5, #0xFFFF, lsl #16
    mov x16, #505
    svc #0x80

    // 6. Present initial frame
    mov x0, x19
    mov x16, #503        // SYS_winios_surface_present
    svc #0x80

    sub sp, sp, #32

counter_loop:
    mov x0, sp
    mov x1, #32
    mov x16, #504        // SYS_winios_poll_event
    svc #0x80

    cmp x0, #1
    b.eq on_touch

    // Yield CPU
    mov x16, #329        // SYS_sched_yield
    svc #0x80
    b counter_loop

on_touch:
    ldr w21, [sp, #0]    // type
    cmp w21, #1          // TouchBegin?
    b.ne counter_loop

    ldr w22, [sp, #8]    // touch.x
    ldr w23, [sp, #12]   // touch.y

    // Check hit on "+1 Increment" button: x in [100, 300], y in [300, 380]
    cmp w22, #100
    b.lt check_exit
    cmp w22, #300
    b.gt check_exit
    cmp w23, #300
    b.lt check_exit
    cmp w23, #380
    b.gt check_exit

    // Increment counter!
    add x20, x20, #1

    mov x0, #1
    adrp x1, msg_counter_inc@PAGE
    add x1, x1, msg_counter_inc@PAGEOFF
    mov x2, #32
    mov x16, #4
    svc #0x80

    // Present surface update
    mov x0, x19
    mov x16, #503
    svc #0x80
    b counter_loop

check_exit:
    // Check hit on "Exit" button: x in [400, 600], y in [300, 380]
    cmp w22, #400
    b.lt counter_loop
    cmp w22, #600
    b.gt counter_loop
    cmp w23, #300
    b.lt counter_loop
    cmp w23, #380
    b.gt counter_loop

    // Exit tapped! Destroy window and exit cleanly
    mov x0, #1
    adrp x1, msg_counter_exit@PAGE
    add x1, x1, msg_counter_exit@PAGEOFF
    mov x2, #26
    mov x16, #4
    svc #0x80

    mov x0, x19
    mov x16, #501        // SYS_winios_window_destroy
    svc #0x80

    add sp, sp, #32
    mov x0, #0
    mov x16, #1          // SYS_exit (1)
    svc #0x80

.section __TEXT,__cstring,cstring_literals
str_counter_title:
    .asciz "Counter"
msg_counter_start:
    .asciz "[APP1] Counter App Initialized\n"
msg_counter_inc:
    .asciz "[APP1] Button Tapped: Counter++\n"
msg_counter_exit:
    .asciz "[APP1] Counter App Exiting\n"
