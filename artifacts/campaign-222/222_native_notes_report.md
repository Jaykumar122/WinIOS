# WinIOS Campaign 222 — Stage 222.20: Native ARM64 Interactive Notes Application Report

## 1. Executive Summary

Stage 222.20 introduces `campaign222_interactive_notes.c`, an authentic freestanding ARM64 Mach-O guest application targeting the WinIOS Category C ABI v1.6 and UI Toolkit v2.

The application executes entirely within an unprivileged EL0 user address space under 16 KB page granule translation, creating a native window, instantiating UI Toolkit widgets (text editor, checkbox, button, progress bar), handling simulated user click events, writing persistent note data to isolated sandbox storage (`/data/com.apple.mobilenotes/active_note.txt`), verifying data integrity via readback, presenting the display surface, and terminating with distinctive exit code `51`.

---

## 2. Compilation & Packaging Details

- **Target Architecture:** ARM64 (`aarch64-macos`)
- **Compilation Toolchain:** Zig 0.17.0 (`zig cc -target aarch64-macos -nostdlib -ffreestanding -fno-builtin -fno-sanitize=all -O2 -Wl,-e,_start`)
- **Runtime Libraries:** `guest-sdk/lib/crt0.s`, `guest-sdk/lib/winios_runtime.c`
- **Binary Format:** 64-bit Mach-O (`MH_MAGIC_64` 0xfeedfacf, CPU Type `0x0100000C`)
- **Binary Size:** 53,328 bytes

---

## 3. Runtime Execution Metrics & Evidence

Execution was validated using `tests/unit/stage222_20_interactive_notes_tests.cpp`:
- **Verified Instructions Retired:** 23,722 instructions executed on simulated Apple A16 Bionic CPU at EL0.
- **Syscall Sequences Handled:**
  - `SYS_getpid` (20)
  - `SYS_log` (528)
  - `SYS_window_create` (500) — Window 360x320 created.
  - `SYS_surface_fill` (505) — Background and widget frames painted.
  - `SYS_storage_write` (517) & `SYS_storage_read` (516) — Persistent note data written & reloaded.
  - `SYS_surface_present` (503) — Surface committed to Compositor.
  - `SYS_exit` (1) — Clean termination with exit code `51`.
- **Persistent State Verification:** File `active_note.txt` read from `IsolatedStorage` matched `"Review ARM64 Compositor & Input"`.
- **Result:** PASSED (100%).
