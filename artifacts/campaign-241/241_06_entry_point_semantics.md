# Stage 241.6: LC_MAIN and LC_UNIXTHREAD Entry Semantics

## 1. Supported Entry Command Semantics
WinIOS supports two distinct Mach-O entry point mechanisms:

1. **`LC_MAIN` (`0x80000028`)**:
   - Standard modern Mach-O entry command produced by Clang/LLVM.
   - Contains relative `entryoff` offset from start of `__TEXT` segment (`entry_point = text_vmaddr + entryoff`).
   - Optional `stacksize` field configured up to maximum 64 KiB default user stack.
2. **`LC_UNIXTHREAD` (`0x00000005`)**:
   - Legacy and freestanding Mach-O entry mechanism.
   - Contains explicit register state (`ARM_THREAD_STATE64`).
   - Entry point is taken directly from the thread state `pc` register at byte offset 272 within command.

## 2. Validation Constraints
- Entry point must be strictly within `__TEXT` segment virtual bounds (`entry_point >= text_vmaddr && entry_point < text_vmaddr + text_vmsize`).
- Entry point must be 4-byte aligned (`entry_point % 4 == 0`).
