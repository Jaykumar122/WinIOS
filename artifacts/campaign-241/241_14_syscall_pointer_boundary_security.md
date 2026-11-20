# Stage 241.14: Syscall Pointer and Memory-Boundary Security

## 1. Memory Boundary Security
- All syscalls accepting pointer arguments (`read`, `write`, `stat64`, `winios_poll_event`) validate:
  1. Pointer is non-null and resides within valid user address space (`>= 0x10000`).
  2. Entire buffer length `[ptr, ptr + len)` is mapped with appropriate permissions (`UserRead` for input, `UserWrite` for output).
  3. No buffer crosses unmapped page boundaries or overflows 64-bit integer limits.
- Host pointers are NEVER directly accepted from guest registers.
