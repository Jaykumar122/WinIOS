# Campaign 191-V Stage V3 — libSystem.B.dylib Subsystem Audit

## 1. Scope and Architectural Role

`libSystem.B.dylib` is Darwin's umbrella dynamic library that provides the C runtime, memory management, POSIX interfaces, and kernel trap gates to every iOS and macOS executable. In WinIOS, [`tests/artifacts/src/libsystem_lite.c`](file:///D:/WinISO26/WinIOS/tests/artifacts/src/libsystem_lite.c) is compiled into a genuine Mach-O dynamic library ([`REAL-018`](file:///D:/WinISO26/WinIOS/tests/artifacts/bin/libSystem.B.dylib)) targeting `LC_DYLD_INFO_ONLY` and `LC_DYLD_CHAINED_FIXUPS`.

## 2. Artifact Identification & Reproducibility

- **Artifact ID:** `REAL-018`
- **File Name:** `libSystem.B.dylib`
- **Binary Path:** `tests/artifacts/bin/libSystem.B.dylib`
- **Size:** 71,920 bytes
- **SHA-256:** `94c66c4d93b10e0d792be637458da41d77d7cbb69e0aba794d3eb3205b2477a2`
- **Compiler:** Zig 0.17.0 (Clang 22.1.8 + Zig Mach-O linker)
- **Compile Flags:** `zig cc -target aarch64-macos -dynamiclib -install_name /usr/lib/libSystem.B.dylib -fPIC -O2`
- **Classification:** `REAL-ARTIFACT-EXECUTED`

## 3. Function-by-Function Implementation & ABI Audit

Every function exported by `REAL-018` was audited for guest vs host execution boundaries:

| Category | Function | Execution Mechanism | Emulation Level | Audit Notes |
|---|---|:---:|:---:|---|
| **Memory Allocation** | `malloc` | Guest ARM64 | GUEST-NATIVE | 16-byte aligned chunk allocator from 4 MB guest heap pool |
| | `free` | Guest ARM64 | GUEST-NATIVE | Header metadata tracking with coalescing and safety validations |
| | `calloc` | Guest ARM64 | GUEST-NATIVE | Zero-initialized allocation (`nmemb * size`) |
| | `realloc` | Guest ARM64 | GUEST-NATIVE | In-place resizing or allocate-copy-free |
| | `posix_memalign` | Guest ARM64 | GUEST-NATIVE | Power-of-two aligned guest memory provisioning |
| **String & Memory** | `memcpy` | Guest ARM64 | GUEST-NATIVE | Unaligned 64-bit and byte-wide transfers |
| | `memmove` | Guest ARM64 | GUEST-NATIVE | Overlap-safe forward/backward buffer transfer |
| | `memset` | Guest ARM64 | GUEST-NATIVE | 64-bit and byte-wide fill |
| | `memcmp` | Guest ARM64 | GUEST-NATIVE | Unsigned byte-by-byte lexicographic comparison |
| | `strlen` | Guest ARM64 | GUEST-NATIVE | Pure guest loop counting bytes until null terminator |
| | `strcpy`, `strncpy` | Guest ARM64 | GUEST-NATIVE | Null-terminating string copy |
| | `strcmp`, `strncmp` | Guest ARM64 | GUEST-NATIVE | Exact character differential comparison |
| | `strcasecmp`, `strncasecmp`| Guest ARM64 | GUEST-NATIVE | Case-insensitive ASCII comparison |
| | `strdup`, `strndup` | Guest ARM64 | GUEST-NATIVE | `malloc` + string copy |
| | `strchr`, `strrchr` | Guest ARM64 | GUEST-NATIVE | Forward and reverse character search |
| | `strstr` | Guest ARM64 | GUEST-NATIVE | Substring pattern matching |
| **Numeric & Format** | `abs`, `labs` | Guest ARM64 | GUEST-NATIVE | Integer magnitude calculation |
| | `strtol`, `strtoul` | Guest ARM64 | GUEST-NATIVE | Base-10/16 string to integer conversion |
| | `strtod` | Guest ARM64 | GUEST-NATIVE | ASCII decimal and scientific notation float parser |
| | `snprintf`, `sprintf` | Guest ARM64 | GUEST-NATIVE | Format string parser (`%d`, `%u`, `%x`, `%s`, `%c`, `%f`) |
| **Darwin Traps** | `write` | `svc #0x80` | HOST-HANDLED TRAP | BSD syscall 4 (`SYS_write`), writes to captured fd |
| | `read` | `svc #0x80` | HOST-HANDLED TRAP | BSD syscall 3 (`SYS_read`) |
| | `pread` | `svc #0x80` | HOST-HANDLED TRAP | BSD syscall 153 (`SYS_pread`), used by `minidyld` |
| | `open`, `close` | `svc #0x80` | HOST-HANDLED TRAP | BSD syscalls 5 (`SYS_open`) and 6 (`SYS_close`) |
| | `lseek` | `svc #0x80` | HOST-HANDLED TRAP | BSD syscall 199 (`SYS_lseek`) |
| | `exit` | `svc #0x80` | HOST-HANDLED TRAP | BSD syscall 1 (`SYS_exit`), records process termination |
| | `abort` | `svc #0x80` | HOST-HANDLED TRAP | Signals termination with exit code 134 |
| **Runtime Security** | `__stack_chk_fail` | Guest ARM64 | GUEST-NATIVE | Calls `abort()` on canary corruption |
| | `__stack_chk_guard` | Guest Data | GUEST-NATIVE | 64-bit entropy initialized by runtime |
| | `__tlv_bootstrap` | Guest ARM64 | GUEST-NATIVE | Computes `TPIDR_EL0` thread pointer descriptor offset |

## 4. Architectural Boundaries and Verification

- **No Host Shortcuts:** No guest calls are intercepted via host magic pointers or debugger traps. All dynamic symbol invocations dispatch to code in `REAL-018` mapped into guest address space.
- **Trap Isolation:** The guest transitions to the host solely at `svc #0x80` instructions. `DarwinUserspaceEngine` validates and services the syscall on the host side, updating `X0` / `NZCV` before resuming guest EL0.
