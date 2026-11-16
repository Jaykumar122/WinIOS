# WinIOS Stage 208.4: Compiler and Linker Reproducibility

## 1. Toolchain Specification
- **Compiler:** Zig 0.17.0 (`C:\Users\singh\AppData\Local\Microsoft\WinGet\Links\zig.exe`)
- **Underlying Frontend:** Clang 19.x (integrated in Zig binary)
- **Target Triple:** `aarch64-macos` (Apple Silicon 64-bit ARM Mach-O)
- **Target Mode:** `-ffreestanding -nostdlib -fno-sanitize=all -fno-stack-protector -O2`

## 2. Compilation Command Line
```cmd
zig cc -target aarch64-macos -ffreestanding -nostdlib -fno-sanitize=all -fno-stack-protector -O2 ^
  -Wl,-e,__start -Wl,-pagezero_size,0 ^
  -o artifacts/campaign-208/xnu_genuine_init.macho ^
  src/kernel/xnu_start.s src/kernel/xnu_pe_init.c src/kernel/xnu_arm_init.c
```

## 3. Linker Parameters & Segment Map
- `-Wl,-e,__start`: Links entry point to symbol `__start` defined in `xnu_start.s`.
- `-Wl,-pagezero_size,0`: Disables 4GB `__PAGEZERO` segment that macOS userspace linkers prepend by default, allowing the kernel executable `__TEXT` segment to sit at virtual address `0x0` or identity-mapped base.
- **Segments Emitted:**
  - `__TEXT`: Contains `.text` executable code (`r-x`).
  - `__DATA`: Contains `.data` and `.bss` globals `g_pe_state`, `g_arm_init_state`, and `bootstrap_stack` (`rw-`).

## 4. Reproducibility & Binary Determinism
- **Repeated Build SHA-256:** `f1b110244844ea8a9a6fd15d51b4232ef29cd91118800f57d1384d9800f15f91`
- **Verification:** Multiple clean rebuilds produce bit-exact identical Mach-O headers and segment contents. Zero timestamps or host path leaks are embedded.
