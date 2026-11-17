// ============================================================================
// WinIOS Campaign 213: Freestanding ARM64 Home-Screen-Like Guest Shell
// Executes genuinely at EL0 under Darwin Mach-O ABI
// ============================================================================
.global _main
.align 4

_main:
    // 1. Announce startup on stdout
    mov x0, #1
    adrp x1, msg_shell_start@PAGE
    add x1, x1, msg_shell_start@PAGEOFF
    mov x2, #40
    mov x16, #4          // SYS_write (4)
    svc #0x80

    // 2. Create home shell window via SYS_winios_window_create (500)
    // args: X0=title_va, X1=title_len, X2=x, X3=y, X4=w, X5=h
    adrp x0, str_title@PAGE
    add x0, x0, str_title@PAGEOFF
    mov x1, #10          // len("Home Shell")
    mov x2, #0           // x = 0
    mov x3, #0           // y = 0
    mov x4, #800         // width = 800
    mov x5, #1200        // height = 1200
    mov x16, #500        // SYS_winios_window_create
    svc #0x80

    // Store Window ID in X19 (callee-saved)
    mov x19, x0

    // 3. Fill background with dark navy/slate (0xFF1A1A2E)
    // SYS_winios_surface_fill (505): X0=wid, X1=x, X2=y, X3=w, X4=h, X5=color
    mov x0, x19
    mov x1, #0
    mov x2, #0
    mov x3, #800
    mov x4, #1200
    movz x5, #0x1A2E
    movk x5, #0xFF1A, lsl #16 // 0xFF1A1A2E
    mov x16, #505
    svc #0x80

    // 4. Fill top status bar banner with pure dark black (0xFF050508)
    mov x0, x19
    mov x1, #0
    mov x2, #0
    mov x3, #800
    mov x4, #60
    movz x5, #0x0508
    movk x5, #0xFF05, lsl #16 // 0xFF050508
    mov x16, #505
    svc #0x80

    // 5. Draw App 1 Icon: "Counter" (Green 0xFF34C759) at [100, 200, 160, 160]
    mov x0, x19
    mov x1, #100
    mov x2, #200
    mov x3, #160
    mov x4, #160
    movz x5, #0xC759
    movk x5, #0xFF34, lsl #16 // 0xFF34C759
    mov x16, #505
    svc #0x80

    // 6. Draw App 2 Icon: "Notes" (Orange 0xFFFF9500) at [350, 200, 160, 160]
    mov x0, x19
    mov x1, #350
    mov x2, #200
    mov x3, #160
    mov x4, #160
    movz x5, #0x9500
    movk x5, #0xFFFF, lsl #16 // 0xFFFF9500
    mov x16, #505
    svc #0x80

    // 7. Present composed shell surface
    // SYS_winios_surface_present (503): X0=wid
    mov x0, x19
    mov x16, #503
    svc #0x80

    // 8. Event loop: poll for touch input events
    // Allocate 32 bytes on stack for GuestInputEvent
    sub sp, sp, #32

event_loop:
    mov x0, sp           // pointer to GuestInputEvent buffer
    mov x1, #32          // buffer size
    mov x16, #504        // SYS_winios_poll_event
    svc #0x80

    // Return value in X0: 1 if event returned, 0 if queue empty
    cmp x0, #1
    b.eq handle_event

    // Queue empty: yield CPU and loop
    mov x16, #329        // SYS_sched_yield
    svc #0x80
    b event_loop

handle_event:
    // Event structure:
    // offset 0:  uint32 type (1=TouchBegin, 2=TouchMove, 3=TouchEnd)
    // offset 8:  int32 x
    // offset 12: int32 y
    ldr w20, [sp, #0]    // type
    cmp w20, #1          // TouchBegin?
    b.ne event_loop

    ldr w21, [sp, #8]    // touch.x
    ldr w22, [sp, #12]   // touch.y

    // Check hit on App 1 Icon (Counter): x in [100, 260], y in [200, 360]
    cmp w21, #100
    b.lt check_app2
    cmp w21, #260
    b.gt check_app2
    cmp w22, #200
    b.lt check_app2
    cmp w22, #360
    b.gt check_app2

    // Tapped App 1!
    mov x0, #1
    adrp x1, msg_tap_app1@PAGE
    add x1, x1, msg_tap_app1@PAGEOFF
    mov x2, #34
    mov x16, #4
    svc #0x80
    b event_loop

check_app2:
    // Check hit on App 2 Icon (Notes): x in [350, 510], y in [200, 360]
    cmp w21, #350
    b.lt event_loop
    cmp w21, #510
    b.gt event_loop
    cmp w22, #200
    b.lt event_loop
    cmp w22, #360
    b.gt event_loop

    // Tapped App 2!
    mov x0, #1
    adrp x1, msg_tap_app2@PAGE
    add x1, x1, msg_tap_app2@PAGEOFF
    mov x2, #32
    mov x16, #4
    svc #0x80
    b event_loop

shell_exit:
    add sp, sp, #32
    mov x0, #0
    mov x16, #1          // SYS_exit
    svc #0x80

.section __TEXT,__cstring,cstring_literals
str_title:
    .asciz "Home Shell"
msg_shell_start:
    .asciz "[SHELL] WinIOS Home Shell Initialized\n"
msg_tap_app1:
    .asciz "[SHELL] Launching Counter App...\n"
msg_tap_app2:
    .asciz "[SHELL] Launching Notes App...\n"
