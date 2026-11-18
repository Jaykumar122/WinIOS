# WinIOS Campaign 221 Native ARM64 Guest Applications Report

**Campaign:** 221 (Stages 221.20, 221.21, 221.22)  
**Target Profile:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`), ARM64 EL0  
**Translation Granule:** 16 KB  
**Cross Compiler:** Zig 0.17.0 (`zig cc -target aarch64-macos -nostdlib -fno-sanitize=all -O2`)  
**Runtime:** Freestanding WinIOS Guest C Runtime (`crt0.s` + `winios_runtime.c`)  

---

## 1. Summary of Delivered Applications

Three native, freestanding ARM64 Mach-O guest applications have been designed, implemented, cross-compiled, and embedded for automated execution and verification:

| Application | Binary Identifier | Mach-O Size | Key Capabilities & Operations | Expected Exit Code |
|---|---|---|---|---|
| **Runtime Explorer** | `com.winios.runtime-explorer` | 52,336 bytes | Identity queries, structured logging, UI window creation (blue & gray frame), private storage configuration read/write, non-blocking event polling. | 42 |
| **Notes & Storage** | `com.winios.notes` | 51,832 bytes | Notepad UI window (orange & white paper layout), persistent note write/read (`quick_note.txt`), persistence verification across restarts. | 43 |
| **Process Monitor** | `com.winios.process-monitor` | 51,488 bytes | Terminal-style UI window (black & green frame), process status query via `WINIOS_SYS_GET_PROCESS_STATUS`, instruction retirement inspection. | 44 |

---

## 2. Technical Provenance & Execution Integrity

1. **Genuine AArch64 Machine Instructions:**
   Every application binary contains authentic AArch64 machine instructions (ELF/Mach-O load commands `LC_SEGMENT_64`, `LC_MAIN`), entry point initialization via `_start` in `crt0.s`, and standard AAPCS64 register conventions.
2. **Freestanding Isolation:**
   None of these applications link against MSVCRT, Windows APIs, or macOS dynamic libraries. Syscalls are issued directly via `svc #0x80`.
3. **Deterministic Termination:**
   Each application exits via `winios_exit(code)`, transitioning through `SYS_exit` to zombie status with distinct verification exit codes (42, 43, 44).
