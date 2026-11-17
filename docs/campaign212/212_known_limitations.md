# WINIOS CAMPAIGN 212 — KNOWN LIMITATIONS & ARCHITECTURAL BOUNDARIES

**Campaign:** 212  
**Baseline Commit:** `9ee8978b54e743945a0b6b547cd1ff5ae2bdc315`  
**Host Toolchain:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Pinned Public XNU Source:** `xnu-10002.1.13`

---

## 1. Scope & Purpose

This document explicitly defines the boundaries, unsupported features, and intentional simplifications within the WinIOS Darwin userspace runtime for Campaign 212. To maintain technical honesty, these items are tracked and separated from verified capabilities.

---

## 2. Architectural & Hardware Limitations

1. **Freestanding Emulator Execution vs Physical A16 Silicon:**
   - Guest instructions are executed by WinIOS's instruction interpreter.
   - Pointers, registers, and memory semantics match AArch64 AAPCS64.
   - Proprietary Apple Neural Engine (ANE), Secure Enclave Processor (SEP), SecureROM, and custom AMX matrix extensions are NOT present or emulated.
2. **Translation Granule & Address Space Limits:**
   - 16 KB page granule (`0x4000` bytes) is strictly enforced. 4 KB granules are not supported for EL0 translation.
   - User address space is bounded to 2 GiB (`0x0000000080000000ULL`) via Stage-1 TCR_EL1/TTBR0_EL1.
   - Virtual memory maps operate within bounded DRAM (8 MiB default for unit test environments, scalable to larger physical allocations).
3. **Register Context Preservation:**
   - `ArmTrapFrame` captures 304 bytes: X0..X30, SP_EL0, ELR_EL1, SPSR_EL1, ESR_EL1, FAR_EL1.
   - FP/SIMD (V0..V31, FPCR, FPSR) and TLS control registers (TPIDR_EL0) are NOT captured in the 304-byte `ArmTrapFrame`. Freestanding binaries must use standard integer calling conventions without active SIMD registers across syscall boundaries.

---

## 3. Darwin Syscall & ABI Limitations

1. **Dual Calling Convention:**
   - Standard Darwin/XNU convention places the syscall number in `X16`, arguments in `X0..X5`, and return value in `X0`.
   - WinIOS Fast ABI places the syscall number in `X0` and arguments in `X1..X5`.
   - When `X16 != 0`, the standard Darwin ABI is prioritized.
2. **Error Flag vs Return Value:**
   - In Apple XNU Darwin ARM64, system calls indicate errors by asserting the carry condition code (`PSTATE.C`) upon return from `svc #0x80`, with `X0` containing a positive errno.
   - WinIOS userspace dispatcher returns negative errno values in `X0` directly (similar to Linux/FreeBSD internal conventions) for deterministic simplicity, while tracking standard Darwin errno definitions (e.g., `-EBADF` = -9, `-EFAULT` = -14, `-ENOSYS` = -78).
3. **Supported Syscall Subset:**
   - Bounded set: `SYS_exit` (1), `SYS_read` (3), `SYS_write` (4), `SYS_open` (5), `SYS_close` (6), `SYS_getpid` (20), `SYS_mach_msg_trap` (31), `SYS_getppid` (39), `SYS_pipe` (42), `SYS_fstat64` (338), `SYS_sched_yield` (329).
   - Complex POSIX calls (`fork`, `execve`, `mmap`, `sigaction`, `ptrace`, `select`, `kqueue`) are either simplified into bounded spawn/lifecycle routines or deferred to future research campaigns.

---

## 4. Loader & Dynamic Linking Boundaries

1. **Mach-O Format Support:**
   - 64-bit Mach-O (`MH_MAGIC_64`, `CPU_TYPE_ARM64`) only. 32-bit Mach-O and fat/universal binaries are rejected.
   - Only `LC_SEGMENT_64`, `LC_MAIN`, `LC_UNIXTHREAD`, and the newly introduced bounded dynamic commands (`LC_LOAD_DYLIB`, `LC_ID_DYLIB`, `LC_RPATH`, `LC_SYMTAB`, `LC_DYSYMTAB`) are processed.
2. **Dynamic Linker Implementation:**
   - Bounded in-engine `GuestDynamicLoader` rather than Apple's proprietary `dyld` or dyld shared cache.
   - Symbol resolution and relocation support is bounded to pointer rebasing and explicit symbol resolution required by guest libraries.
   - Apple dyld shared cache (`/System/Library/Caches/com.apple.dyld/...`) is explicitly out of scope.

---

## 5. Process Lifecycle & IPC Boundaries

1. **Process Creation Model:**
   - Uses `posix_spawn`-like controlled process instantiation (`create_process` with independent `UserAddressSpace` and address mappings), rather than traditional copy-on-write `fork()`.
2. **Mach IPC:**
   - Provides a clean, bounded Mach port implementation with send/receive/dead-name states and bounded message queues.
   - Complex out-of-line memory descriptors, port right arrays, and audit tokens are not included.
