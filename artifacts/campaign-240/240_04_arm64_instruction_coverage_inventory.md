# Stage 240.4: ARM64 Instruction Coverage Inventory

## 1. Instruction Family Matrix
The WinIOS ARM64 CPU interpreter (`src/cpu/arm64_cpu.cpp`) implements freestanding user-mode (EL0) execution fidelity:

| Instruction Family | Implemented Status | Opcode Examples | Notes |
| :--- | :--- | :--- | :--- |
| **Integer Arithmetic** | Implemented | `ADD`, `ADDS`, `SUB`, `SUBS`, `CMP`, `NEG` | 32-bit and 64-bit forms, flag setting (N, Z, C, V) |
| **Logical Operations** | Implemented | `AND`, `ANDS`, `ORR`, `EOR`, `TST`, `MVN` | Bitwise logic, immediate and register forms |
| **Data Movement** | Implemented | `MOV`, `MOVZ`, `MOVN`, `MOVK` | Immediate loading, bitfield shifts |
| **Multiply / Divide** | Implemented | `MUL`, `MADD`, `MSUB`, `SDIV`, `UDIV` | Signed/unsigned 32/64-bit integer division |
| **Branch & Control** | Implemented | `B`, `B.cond`, `BL`, `BLR`, `RET`, `CBZ`, `CBNZ` | Direct/indirect branches, link register updates |
| **Load / Store** | Implemented | `LDR`, `STR`, `LDRB`, `STRB`, `LDRH`, `STRH` | Immediate offset, pre-index, post-index, unscaled |
| **Pair Load / Store** | Implemented | `LDP`, `STP` | Register-pair push/pop for AAPCS64 frames |
| **Address Generation** | Implemented | `ADR`, `ADRP` | PC-relative 4KB/16KB page addressing |
| **System Calls / Traps** | Implemented | `SVC #0` | Exception level elevation to host kernel dispatcher |
| **Floating Point** | Partially Implemented | `FADD`, `FSUB`, `FMUL`, `FDIV`, `FMOV`, `FCMP` | Single and double precision IEEE 754 |
| **SIMD / Vector** | Unsupported | `ADD` (vector), `NEON` ops | Trapped deterministically as unhandled instruction |
| **System Registers** | Restricted | `MRS`, `MSR` (`TPIDR_EL0`, `NZCV`) | EL0 thread pointer and flags; EL1/EL2/EL3 rejected |

## 2. Honesty and Scope
WinIOS provides execution fidelity for freestanding ARM64 Mach-O binaries required by the WinIOS SDK. It does not claim full ARMv8.5+ architectural completeness, SVE, or virtualization extensions.
