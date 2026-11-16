# WinIOS Campaign 205 — Stage 205.10 Report
## ARM64 Exception Vector Initialization & Syndrome Decoding

**Stage:** 205.10  
**Status:** COMPLETE / PASS  
**Date:** 2026-11-16  
**Host:** Windows 11 x64  
**Compiler:** Visual Studio 2026 / MSVC C++20 (`/W4 /WX /permissive-`)  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)

---

### 1. Architectural Summary

In ARM64 EL1 kernel execution, the CPU vectors all synchronous and asynchronous exceptions through the table pointed to by `VBAR_EL1`. This table has strict architectural requirements:
1. **Alignment:** The base of `VBAR_EL1` must be aligned to 2048 bytes (bits [10:0] are RES0).
2. **Vector Layout:** Exactly 16 exception vector entry points divided into 4 origin categories (each 512 bytes):
   - `CurrentEL_SP0` (0x000 - 0x180): Exceptions taken from Current EL with SP_EL0 selected.
   - `CurrentEL_SPx` (0x200 - 0x380): Exceptions taken from Current EL with SP_ELx selected (kernel stack).
   - `LowerEL_AArch64` (0x400 - 0x580): Exceptions taken from lower EL executing in 64-bit state (userspace).
   - `LowerEL_AArch32` (0x600 - 0x780): Exceptions taken from lower EL executing in 32-bit state.
3. **Slot Offsets:** Each origin has 4 vectors (128 bytes = 32 instructions each):
   - Synchronous (`+0x00`)
   - IRQ (`+0x80`)
   - FIQ (`+0x100`)
   - SError (`+0x180`)

### 2. Implementation Deliverables

- **`src/cpu/kernel_exception_vectors.h` & `.cpp`:**
  - `compute_vector_offset(origin, type)`: Compile-time constant expression calculating the exact byte offset from `VBAR_EL1`.
  - `validate_vector_table_alignment(vbar_el1, out_error)`: Validates that the proposed `VBAR_EL1` meets the 2048-byte alignment restriction.
  - `decode_esr(esr)`: Decodes `ESR_EL1` into Exception Class (EC, bits [31:26]), Instruction Length (IL, bit [25]), and Instruction Specific Syndrome (ISS, bits [24:0]), providing human-readable diagnostics for traps, aborts, and SVCs.
  - `build_default_vector_table()`: Generates a freestanding 2048-byte vector table pre-populated with safe stub instructions (`NOP` followed by `ERET`).

### 3. Verification & Test Results

The test suite `tests/unit/stage205_10_kernel_exception_vectors_tests.cpp` was integrated into CTest and executed:
- All 16 vector offset computations verified against ARMv8-A architecture specification.
- Alignment validation tested against aligned (0x0, 0x800, 0x1000, 0xFFFF'8000'0000'0800) and unaligned addresses (0x1, 0x7FC, 0x804).
- ESR decoding verified for SVC AArch64 (0x15), Data Aborts (0x25), Instruction Aborts (0x20), SP alignment faults (0x26), and BRK exceptions (0x3C).
- Vector table generation verified to produce exactly 2048 bytes (512 instructions) with proper slot spacing.

**Test Status:** 5/5 sub-tests PASSED (0 failures, 0 warnings).
