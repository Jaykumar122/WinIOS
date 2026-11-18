# WinIOS Minimal Freestanding C Runtime Report

**Campaign:** 221 (Stage 221.4)  
**Target:** ARM64 (AArch64 EL0) Freestanding Guest Applications  
**Toolchain:** Zig 0.17.0 (`zig cc -target aarch64-macos -nostdlib`)  

---

## 1. Objectives & Isolation Policy

In previous campaigns, guest applications often relied on synthetic host stubs or minimal inline assembly. Campaign 221 introduces a standalone, complete freestanding C runtime that operates entirely without:
1. The Microsoft Visual C++ Runtime (MSVCRT / UCRT).
2. Host C++ exceptions or RTTI.
3. Apple proprietary libraries (`libSystem.B.dylib` or Xcode SDKs).

All memory, string, formatting, argument, and lifecycle primitives are self-contained in `winios_runtime.c` and `crt0.s`.

---

## 2. Implemented Primitives

### 2.1 Memory Manipulation
- `winios_memcpy`: Word-aligned 64-bit fast path for non-overlapping copies with byte fallback.
- `winios_memmove`: Supports arbitrary source and destination overlaps (detects direction: `dest < src` forward, `dest > src` backward).
- `winios_memset`: Word-aligned 64-bit fill pattern.
- `winios_memcmp`: Standard lexicographical byte comparison.

### 2.2 String Operations
- `winios_strlen`: Pointer-difference length calculation.
- `winios_strcmp` & `winios_strncmp`: Safe bounded null-terminated comparisons.
- `winios_strcpy` & `winios_strncpy`: Standard copying with explicit zero-padding.

### 2.3 Formatting and Conversions
- `winios_itoa`: Decimal and hexadecimal signed integer formatting.
- `winios_atoi`: Base-10 string-to-integer parser with whitespace and sign handling.
- `winios_u64_to_hex`: Fixed 18-character `0x` hexadecimal string formatter.

### 2.4 Entry Adapter & Exit
- `_start` (`crt0.s`): Preserves initial `X0` (pointing to `GuestAppArgBlock`), establishes frame base, initializes runtime globals, resolves `argc`/`argv`, invokes `main()`, and dispatches exit status to `SYS_exit` via `svc #0x80`.

---

## 3. Supported vs Unsupported Standard C Matrix

| Standard Function | WinIOS Freestanding Support | Implementation Notes |
|---|---|---|
| `memcpy` | **Supported** (`winios_memcpy`) | 64-bit aligned optimization. |
| `memmove` | **Supported** (`winios_memmove`) | True overlapping buffer support. |
| `memset` | **Supported** (`winios_memset`) | 64-bit pattern broadcast. |
| `memcmp` | **Supported** (`winios_memcmp`) | Unsigned byte comparison. |
| `strlen` | **Supported** (`winios_strlen`) | Standard semantics. |
| `strcmp` / `strncmp` | **Supported** | Standard semantics. |
| `strcpy` / `strncpy` | **Supported** | Standard semantics. |
| `atoi` / `itoa` | **Supported** | Custom minimal implementation. |
| `malloc` / `free` | **Supported via Kernel Allocator** | Heap bounds supplied in `GuestAppArgBlock`. |
| `printf` / `vsnprintf` | **Unsupported (Direct stdio)** | Structured logging via `winios_log` or `SYS_write` to FD 1/2. |
| `setjmp` / `longjmp` | **Unsupported** | Requires complex architectural context restore. |
| `pthread_*` | **Unsupported (Direct posix)** | Use WinIOS thread syscalls (`SYS_bsdthread_create`). |
