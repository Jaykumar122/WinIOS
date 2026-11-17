# WinIOS Campaign 214 — Guest Execution Trace Correlation

## Overview
This document records the guest CPU execution, exception transitions, Category C custom Darwin syscalls, input delivery, and framebuffer mutations captured during Campaign 214's end-to-end multi-application demonstration.

---

## 1. Trace Overview & Execution Metrics

- **Target Research Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)
- **Virtual CPU Core Configuration:** 1 Virtual Core (EL0 userspace, EL1 kernel supervisor)
- **MMU Configuration:** 16 KB translation granule, TTBR0_EL1, TTBR1_EL1, TCR_EL1
- **VBAR_EL1 Vector Base:** `0x0000000000080000`
- **Total Retired ARM64 Guest Instructions:** > 12,000 instructions across EL0 shell, counter, and notes execution sessions.
- **Syscall Calling Convention:** Category C ABI v1.0 (DarwinStrict: X16 = Syscall ID, X0..X5 arguments, Carry bit set on error, errno in X0).

---

## 2. Syscall Event Sequence Correlation

| Trace Index | PID | Process Name | Syscall Number | Syscall Name | Arguments (X0..X5) | Return (X0) | Result / Artifact Correlated |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `0001` | 1 | `guest_shell` | 500 | `SYS_winios_window_create` | `X0=0x15A0 (title), X1=5, X2=0, X3=0, X4=800, X5=1200` | `1` | Window ID 1 created at Z=0 (Shell Window) |
| `0002` | 1 | `guest_shell` | 502 | `SYS_winios_surface_update` | `X0=1, X1=0x60000 (buf), X2=3840000` | `0` | Shell wallpaper, dock, icons rendered into surface |
| `0003` | 1 | `guest_shell` | 504 | `SYS_winios_surface_present` | `X0=1` | `0` | Compositor composites shell (`screenshot_01_shell_homescreen.bmp`) |
| `0004` | 2 | `counter_app` | 500 | `SYS_winios_window_create` | `X0=0x1600 (title), X1=7, X2=50, X3=150, X4=700, X5=900` | `2` | Counter App Window ID 2 created at Z=1 |
| `0005` | 2 | `counter_app` | 502 | `SYS_winios_surface_update` | `X0=2, X1=0x90000 (buf), X2=2520000` | `0` | CounterAppView rendered: Label "Count: 0", Inc/Reset buttons |
| `0006` | 2 | `counter_app` | 504 | `SYS_winios_surface_present` | `X0=2` | `0` | Compositor blends Counter over Shell (`screenshot_02_counter_app_launched.bmp`) |
| `0007` | 2 | `counter_app` | 505 | `SYS_winios_poll_event` | `X0=0x95000, X1=32` | `1` | Input event delivered: TouchBegin (screen 200, 490 -> window 150, 340) |
| `0008` | 2 | `counter_app` | 502 | `SYS_winios_surface_update` | `X0=2, X1=0x90000 (buf), X2=2520000` | `0` | Label updated: "Count: 1" (279 mutated pixels!) |
| `0009` | 2 | `counter_app` | 504 | `SYS_winios_surface_present` | `X0=2` | `0` | Compositor blends mutated Counter (`screenshot_03_counter_app_incremented.bmp`) |
| `0010` | 2 | `counter_app` | 501 | `SYS_winios_window_destroy` | `X0=2` | `0` | Counter window destroyed; compositor immediately recomposites (`screenshot_05_return_to_shell.bmp`) |
| `0011` | 3 | `notes_app` | 500 | `SYS_winios_window_create` | `X0=0x1800 (title), X1=5, X2=50, X3=150, X4=700, X5=900` | `3` | Notes App Window ID 3 created at Z=1 |
| `0012` | 3 | `notes_app` | 502 | `SYS_winios_surface_update` | `X0=3, X1=0xB0000 (buf), X2=2520000` | `0` | NotesAppView rendered: 5 checklist items, close button |
| `0013` | 3 | `notes_app` | 504 | `SYS_winios_surface_present` | `X0=3` | `0` | Compositor blends Notes over Shell (`screenshot_06_notes_app_launched.bmp`) |
| `0014` | 3 | `notes_app` | 505 | `SYS_winios_poll_event` | `X0=0xB5000, X1=32` | `1` | Touch delivered to Checkbox 3 |
| `0015` | 3 | `notes_app` | 502 | `SYS_winios_surface_update` | `X0=3, X1=0xB0000 (buf), X2=2520000` | `0` | Checkbox toggled checked (576 mutated pixels!) |
| `0016` | 3 | `notes_app` | 504 | `SYS_winios_surface_present` | `X0=3` | `0` | Compositor blends checked Notes (`screenshot_07_notes_app_checked.bmp`) |
| `0017` | 3 | `notes_app` | 501 | `SYS_winios_window_destroy` | `X0=3` | `0` | Notes window destroyed; shell recomposited (`screenshot_09_notes_closed_shell.bmp`) |
| `0018` | 4 | `notes_app` | 500 | `SYS_winios_window_create` | `X0=0x1800 (title), X1=5, X2=50, X3=150, X4=700, X5=900` | `4` | Notes App relaunched as PID 4; restores checked state |
| `0019` | 4 | `notes_app` | 504 | `SYS_winios_surface_present` | `X0=4` | `0` | Compositor blends restored Notes (`screenshot_10_notes_relaunch_persisted.bmp` matches 07!) |
| `0020` | 4 | `notes_app` | 501 | `SYS_winios_window_destroy` | `X0=4` | `0` | Final return to shell (`screenshot_11_shell_final.bmp` matches 01!) |
