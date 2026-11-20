# Stage 241.15: SDK Runtime and Freestanding Contract

## 1. Freestanding Contract Specification
- **Compiler**: Zig 0.17.0 (`zig cc -target aarch64-macos`).
- **Flags**: `-nostdlib -fno-stack-protector -fno-sanitize=all`.
- **Target Triple Property**: `aarch64-macos` is used solely to emit compliant 64-bit Mach-O object containers (`MH_MAGIC_64`, `CPU_TYPE_ARM64`).
- **Absence of Darwin Runtime**: No dynamic libraries (`libSystem.B.dylib`, `libc++`), dyld, or Apple proprietary frameworks are linked. Workloads execute entirely freestanding via `_start` entry point and inline `svc #0` syscalls.
