# WINIOS CAMPAIGN 218 — STAGE 218.8 ARM64 INSTRUCTION COVERAGE & COMPATIBILITY AUDIT

**Date:** November 18, 2026  
**Campaign:** 218 (Stage 218.8)  
**Host:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Cross Toolchain:** Windows Zig 0.17.0 (`-target aarch64-macos -nostdlib -fno-sanitize=undefined -O2 -fno-vectorize -fno-slp-vectorize`)  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), 16 KB Granule

---

## 1. Compiler Output Instruction Disassembly Analysis

Disassembly of compiler-generated freestanding ARM64 Mach-O binaries produced the following concrete opcode patterns across arithmetic, stack manipulation, branches, and memory load/stores:

| Category | ARM64 Assembly | Opcode Pattern (Hex) | Decoder / Executor Status in `src/cpu/cpu.cpp` |
|---|---|---|---|
| Return | `RET` | `0xD65F03C0` | VERIFIED & RETIRED |
| Move Immediate | `MOV W0, #imm` / `MOVZ` | `0x5280xxxx` | VERIFIED & RETIRED |
| Move Immediate 64 | `MOV X0, #imm` / `MOVZ` | `0xD280xxxx` | VERIFIED & RETIRED |
| Compare Immediate | `CMP W0, #imm` | `0x7100xxxx` | VERIFIED & RETIRED |
| Conditional Branch | `B.EQ` / `B.NE` / `B.LE` / `B.GT` | `0x5400xxxx` | VERIFIED & RETIRED |
| Unconditional Branch | `B offset` | `0x1400xxxx` | VERIFIED & RETIRED |
| Function Call | `BL offset` | `0x9400xxxx` | VERIFIED & RETIRED |
| Exclusive OR | `EOR Wd, Wn, Wm` | `0x4A00xxxx` | VERIFIED & RETIRED |
| Add (Extended/Shifted) | `ADD Wd, Wn, Wm` | `0x0B00xxxx` | VERIFIED & RETIRED |
| Multiply | `MUL Wd, Wn, Wm` / `MADD` | `0x1B00xxxx` | VERIFIED & RETIRED |
| Bitwise Shift Right | `ASR` / `LSR` / `UBFM` | `0x5300xxxx` | VERIFIED & RETIRED |
| Store Register Pair | `STP Xt1, Xt2, [SP, #-16]!` | `0xA9BFxxxx` | VERIFIED & RETIRED |
| Load Register Pair | `LDP Xt1, Xt2, [SP], #16` | `0xA8C1xxxx` | VERIFIED & RETIRED |
| Store Single Register | `STR Wt, [Xn, #offset]` | `0xB900xxxx` | VERIFIED & RETIRED |
| Load Single Register | `LDR Wt, [Xn, #offset]` | `0xB940xxxx` | VERIFIED & RETIRED |
| Load 64-bit Register | `LDR Xt, [Xn, #offset]` | `0xF940xxxx` | VERIFIED & RETIRED |
| System Call Trap | `SVC #0x80` / `SVC #0` | `0xD4001001` / `0xD4000001` | VERIFIED & TRAPPED TO EL1 |

---

## 2. Instruction Retirement Verification

- The test workload `src/apps/guest_test_workload.c` was compiled into an ARM64 Mach-O binary and loaded into guest memory.
- Stepped through 100+ CPU instruction cycles on the virtual CPU.
- Verified register state progression:
  - Recursive stack frames allocated and deallocated cleanly via `STP`/`LDP`.
  - Condition flags (`NZCV`) evaluated properly for recursion termination.
  - Factorial, arithmetic, bitwise XOR, and stack memory load/stores computed deterministically:
    $$f(5) + \text{compute\_math}(10, 8) + \text{test\_memory\_ops}() = 120 + 120 + 100 = 340$$
  - Register `X0` returned `340` upon executing `RET`.
