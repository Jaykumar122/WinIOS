# Stage 206.9: CPU Feature & System-Register Audit Report

## 1. Executive Summary
- **Stage:** 206.9 — CPU Feature & System-Register Audit
- **Status:** PASS
- **Date:** 2026-11-16
- **Architecture Target:** ARMv8.6-A / ARM64e Apple A16 Bionic (`T8120`)
- **Components Audited:** Core architectural system registers and Apple implementation-defined registers (`S3_...`) accessed along the early Darwin/XNU boot path.

---

## 2. System-Register Access Audit on Early XNU Bootstrap Path

| Register Mnemonic | Key / Encoding (op0,op1,CRn,CRm,op2) | Execution EL | Access Dir | Function in `start.s` | Implementation Status |
|---|---|---|---|---|---|
| `DAIFSet` | `0xD5034FDF` (Immediate) | EL1 | Write | Masks FIQ, IRQ, SError, Debug exceptions at entry | Fully Implemented |
| `MPIDR_EL1` | `(3, 0, 0, 0, 5)` | EL1 | Read | Reads CPU cluster/core affinity (0x80000000 on Core 0) | Fully Implemented |
| `VBAR_EL1` | `(3, 0, 12, 0, 0)` | EL1 | Read/Write | Sets exception vector base address (must be 2KB aligned) | Fully Implemented |
| `TCR_EL1` | `(3, 0, 2, 0, 2)` | EL1 | Read/Write | 16KB granule configuration: TG0=`0b10`, TG1=`0b01`, T0SZ/T1SZ | Fully Implemented |
| `TTBR0_EL1` | `(3, 0, 2, 0, 0)` | EL1 | Read/Write | Identity translation table base address | Fully Implemented |
| `TTBR1_EL1` | `(3, 0, 2, 0, 1)` | EL1 | Read/Write | Kernel higher-half translation table base address | Fully Implemented |
| `MAIR_EL1` | `(3, 0, 10, 2, 0)`| EL1 | Read/Write | Memory attribute indirection (Device-nGnRnE, Normal WB-WA) | Fully Implemented |
| `SCTLR_EL1` | `(3, 0, 1, 0, 0)` | EL1 | Read/Write | MMU enable (M=1), Caches (C=1, I=1), WXN, SPAN, SED | Fully Implemented |
| `CPACR_EL1` | `(3, 0, 1, 0, 2)` | EL1 | Read/Write | Architectural FP/NEON enable (FPEN=`0b11`) | Fully Implemented |
| `TPIDR_EL1` | `(3, 0, 13, 0, 4)`| EL1 | Read/Write | Per-CPU / thread data pointer in kernel mode | Fully Implemented |
| `TPIDRRO_EL0` | `(3, 3, 13, 0, 3)`| EL1 | Read/Write | Read-only thread pointer for userspace commpage | Fully Implemented |
| `FAR_EL1` | `(3, 0, 6, 0, 0)` | EL1 | Read/Write | Fault address capture on aborts | Fully Implemented |
| `ESR_EL1` | `(3, 0, 5, 2, 0)` | EL1 | Read/Write | Exception syndrome capture (EC, ISS) | Fully Implemented |
| `ELR_EL1` | `(3, 0, 4, 0, 1)` | EL1 | Read/Write | Return PC from exception | Fully Implemented |
| `SPSR_EL1` | `(3, 0, 4, 0, 0)` | EL1 | Read/Write | Saved PSTATE on exception entry | Fully Implemented |

---

## 3. Apple Implementation-Defined Registers (IMP DEF)
Apple Silicon incorporates proprietary system registers under `op0 = 3, op1 = 0..7, crn = 15`:
1. `SYS_APL_HID0`..`HID11` (`s3_0_c15_c0_0`..`s3_0_c15_c11_0`): Hardware implementation-defined tuning registers.
2. `SYS_APL_CYC_OVRD` (`s3_0_c15_c12_0`): Cycle-count override.
3. `SYS_APL_ACC_OVRD` (`s3_0_c15_c12_1`): Access override.
4. `SPRR_CONFIG_EL1` / `GXF_CONFIG_EL1`: Guarded Execution / Permission Overlay registers.

**Isolation & Compatibility Strategy:**
- These registers are classified in `winios::darwin::DarwinCompatibilityRegistry` under `Classification::PubliclyDocumentedDarwin` or `Classification::ResearchOnly`.
- When accessed during research boot, access is audited and logged, preventing unexpected Undefined Instruction exceptions while maintaining isolation from generic ARM64 execution.
