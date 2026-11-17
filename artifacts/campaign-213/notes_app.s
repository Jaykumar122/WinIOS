// ============================================================================
// WinIOS Campaign 213: Freestanding ARM64 Interactive Notes App
// Executes genuinely at EL0 under Darwin Mach-O ABI
// ============================================================================
.global _main
.align 4

_main:
    // 1. Announce startup on stdout
    mov x0, #1
    adrp x1, msg_notes_start@PAGE
    add x1, x1, msg_notes_start@PAGEOFF
    mov x2, #30
    mov x16, #4          // SYS_write (4)
    svc #0x80

    // 2. Create Notes window via SYS_winios_window_create (500)
    adrp x0, str_notes_title@PAGE
    add x0, x0, str_notes_title@PAGEOFF
    mov x1, #5           // len("Notes")
    mov x2, #50          // x = 50
    mov x3, #150         // y = 150
    mov x4, #700         // w = 700
    mov x5, #900         // h = 900
    mov x16, #500
    svc #0x80

    mov x19, x0          // X19 = window ID
    mov x20, #0          // X20 = note check state (0=unchecked, 1=checked)

    // 3. Fill window with notepad cream background (0xFFFFFDF0)
    mov x0, x19
    mov x1, #0
    mov x2, #0
    mov x3, #700
    mov x4, #900
    movz x5, #0xFDF0
    movk x5, #0xFFFF, lsl #16
    mov x16, #505        // SYS_winios_surface_fill
    svc #0x80

    // 4. Draw header banner (Yellow 0xFFFFD60A) at [0, 0, 700, 80]
    mov x0, x19
    mov x1, #0
    mov x2, #0
    mov x3, #700
    mov x4, #80
    movz x5, #0xD60A
    movk x5, #0xFFFF, lsl #16
    mov x16, #505
    svc #0x80

    // 5. Draw Note 1 Checkbox at [50, 200, 50, 50] (Gray 0xFFC7C7CC)
    mov x0, x19
    mov x1, #50
    mov x2, #200
    mov x3, #50
    mov x4, #50
    movz x5, #0xC7CC
    movk x5, #0xFFC7, lsl #16
    mov x16, #505
    svc #0x80

    // 6. Draw Note 1 Line at [120, 220, 500, 10] (Slate 0xFF8E8E93)
    mov x0, x19
    mov x1, #120
    mov x2, #220
    mov x3, #500
    mov x4, #10
    movz x5, #0x8E93
    movk x5, #0xFF8E, lsl #16
    mov x16, #505
    svc #0x80

    // 7. Draw "Close Note" button at [250, 700, 200, 70] (Orange 0xFFFF9500)
    mov x0, x19
    mov x1, #250
    mov x2, #700
    mov x3, #200
    mov x4, #70
    movz x5, #0x9500
    movk x5, #0xFFFF, lsl #16
    mov x16, #505
    svc #0x80

    // 8. Present surface
    mov x0, x19
    mov x16, #503        // SYS_winios_surface_present
    svc #0x80

    sub sp, sp, #32

notes_loop:
    mov x0, sp
    mov x1, #32
    mov x16, #504        // SYS_winios_poll_event
    svc #0x80

    cmp x0, #1
    b.eq on_notes_touch

    // Yield CPU
    mov x16, #329        // SYS_sched_yield
    svc #0x80
    b notes_loop

on_notes_touch:
    ldr w21, [sp, #0]    // type
    cmp w21, #1          // TouchBegin?
    b.ne notes_loop

    ldr w22, [sp, #8]    // touch.x
    ldr w23, [sp, #12]   // touch.y

    // Check hit on Checkbox: x in [50, 100], y in [200, 250]
    cmp w22, #50
    b.lt check_close
    cmp w22, #100
    b.gt check_close
    cmp w23, #200
    b.lt check_close
    cmp w23, #250
    b.gt check_close

    // Toggle note check state!
    eor x20, x20, #1

    // Fill checkbox with Green (0xFF34C759)
    mov x0, x19
    mov x1, #50
    mov x2, #200
    mov x3, #50
    mov x4, #50
    movz x5, #0xC759
    movk x5, #0xFF34, lsl #16
    mov x16, #505
    svc #0x80

    mov x0, #1
    adrp x1, msg_note_checked@PAGE
    add x1, x1, msg_note_checked@PAGEOFF
    mov x2, #24
    mov x16, #4
    svc #0x80

    mov x0, x19
    mov x16, #503        // SYS_winios_surface_present
    svc #0x80
    b notes_loop

check_close:
    // Check hit on "Close Note" button: x in [250, 450], y in [700, 770]
    cmp w22, #250
    b.lt notes_loop
    cmp w22, #450
    b.gt notes_loop
    cmp w23, #700
    b.lt notes_loop
    cmp w23, #770
    b.gt notes_loop

    // Close tapped!
    mov x0, #1
    adrp x1, msg_notes_exit@PAGE
    add x1, x1, msg_notes_exit@PAGEOFF
    mov x2, #24
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
str_notes_title:
    .asciz "Notes"
msg_notes_start:
    .asciz "[APP2] Notes App Initialized\n"
msg_note_checked:
    .asciz "[APP2] Note Item Checked\n"
msg_notes_exit:
    .asciz "[APP2] Notes App Exiting\n"
