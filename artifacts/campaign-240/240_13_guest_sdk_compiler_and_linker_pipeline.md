# Stage 240.13: Guest SDK Compiler and Linker Pipeline

## 1. Toolchain Pipeline Specification
- **Compiler**: Zig 0.17.0 (`zig cc`)
- **Target**: `aarch64-macos`
- **Compiler Flags**:
  - `-nostdlib`: Prevents linking host or Darwin default libc.
  - `-fno-stack-protector`: Prevents emitting unlinked `___stack_chk_guard` GOT references in freestanding mode.
  - `-fno-sanitize=all`: Eliminates undefined runtime sanitizer symbol dependencies.
  - `-I WinIOS/guest-sdk/include`: Exposes SDK headers (`winios/entry.h`, `winios/syscall.h`, `winios/winios_types.h`).
- **Linker Flags**:
  - `-Wl,-e,_start`: Sets canonical freestanding entry point.
  - `-Wl,-pagezero_size,0x10000`: Explicit 64 KB page zero for null pointer trapping.
- **Artifact Verification**:
  - All compiled `.macho` files verified to contain `MH_MAGIC_64` and `CPU_TYPE_ARM64`.
