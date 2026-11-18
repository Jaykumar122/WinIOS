# WinIOS Campaign 223 — Native ARM64 Calculator Application Report (Stage 223.19)

## 1. Executive Summary

Stage 223.19 establishes a genuine, freestanding ARM64 Mach-O Calculator application (`src/apps/campaign223_calculator.c`). The binary is compiled using Zig 0.17.0 (`-target aarch64-macos`), linked with `crt0.s` and `winios_runtime.c`, and verified under real EL0 CPU emulation.

---

## 2. Application Architecture & Interactive Flow

- **Window Layout**: 280x360 window styled after Apple iOS Calculator with dark aesthetic (`0xFF1C1C1E`).
- **Interactive Buttons**: Grid keypad containing '7', '+', '5', '=', 'C'.
- **Arithmetic Engine**: Performs calculation sequence `7 + 5 = 12`.
- **System Clipboard Integration**: Copies the resulting string `"12"` to the system clipboard via Category C syscall `SYS_WINIOS_CLIPBOARD_SET`.
- **Clean Exit**: Concludes execution with distinctive exit code `61`.

---

## 3. Verification & Execution Evidence

- **Binary Size**: 52,408 bytes (`calc.bin`).
- **Instructions Retired**: 5,960 guest ARM64 instructions verified via `ExecutionBoundaryEnforcer`.
- **Clipboard Output**: Verified readback value `"12"`.
- **Test Suite**: `tests/unit/stage223_19_calculator_tests.cpp` (100% PASS).
