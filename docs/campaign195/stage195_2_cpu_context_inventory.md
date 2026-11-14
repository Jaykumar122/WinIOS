# Stage 195.2 — Architectural CPU Context Inventory

## 1. Scope & Architectural Model

This document establishes the canonical inventory of supported ARM64 guest CPU state in WinIOS, defining the exact boundaries between per-CPU, per-thread, per-address-space, and per-exception-frame state.

---

## 2. Canonical State Categorization

| Architectural Element | Registers / State | Storage Location | Switch Scope | Preservation Mechanism |
| :--- | :--- | :--- | :--- | :--- |
| **GPRs (Callee-Saved)** | `X19–X28`, `X29 (FP)`, `X30 (LR)` | `GuestTcb`, `ExceptionFrame` | Per Thread | `cpu_switch_to` / `ExceptionFrame` |
| **GPRs (Caller-Saved)** | `X0–X18` | `ExceptionFrame` | Per Exception Frame | Pushed to stack upon interrupt / fault |
| **Stack Pointers** | `SP_EL0` (user), `SP_EL1` (kernel) | `GuestTcb.sp_el0/1`, `CPU` | Per Thread | Banked `sp_el0_` / `sp_el1_` in `CPU` |
| **Instruction Pointer**| `PC`, `ELR_EL1` | `GuestTcb.pc`, `ExceptionFrame` | Per Thread | Saved into `ELR_EL1` on exception, restored via `ERET` or `BR` |
| **Condition Flags** | `NZCV` (Negative, Zero, Carry, oVerflow) | `PSTATE` / `SPSR_EL1` | Per Thread | Preserved in `SPSR_EL1`, restored via `ERET` / context load |
| **Interrupt Masks** | `DAIF` (Debug, SError, IRQ, FIQ) | `PSTATE` / `SPSR_EL1` | Per CPU / Per Thread | Masked on exception entry, restored via `ERET` |
| **SIMD / Vector** | `Q0–Q31` (32x 128-bit vector registers) | `ArchitecturalCpuContext`, `ExceptionFrame` | Per Thread | Saved in 512-byte vector block in `ExceptionFrame` |
| **FP Control / Status**| `FPCR`, `FPSR` | `ExceptionFrame`, `ArchitecturalCpuContext` | Per Thread | Stored at offsets `0x060` and `0x064` in `ExceptionFrame` |
| **Thread Pointers** | `TPIDR_EL0`, `TPIDR_EL1` | `CPU`, `ExceptionFrame` | Per Thread | Swapped during context switch / restored from frame |
| **Address Space** | `ASID`, `TTBR0_EL1` | `CPU.asid_`, `CPU.ttbr0_el1_` | Per Address Space | Set during task activation |
| **Exception Info** | `ESR_EL1`, `FAR_EL1` | `ExceptionFrame` | Per Exception Entry | Architectural exception syndrome and fault address |

---

## 3. Structural Definitions

### 3.1 Cooperative Context Switching (`cpu_switch_to`)
- **Applies:** At voluntary yield / function call boundaries (e.g. `thread_yield`, blocking mutex).
- **Registers:** AAPCS64 callee-saved registers `X19–X28`, `FP`, `LR`, and `SP`.
- **Footprint:** 88 bytes executed by 22 ARM64 guest instructions.

### 3.2 Asynchronous Preemption (`ExceptionFrame`)
- **Applies:** At arbitrary instruction boundary preemption (virtual timer PPI 27, SGI 1, hardware interrupts).
- **Structure:** 896-byte strictly aligned frame (`ExceptionFrame`).
- **Invariant:** Completely preserves all 31 GPRs (`X0–X30`), condition flags (`NZCV`), interrupt masks (`DAIF`), thread pointers (`TPIDR_EL0/1`), address-space configuration, and all 32 128-bit SIMD registers (`Q0–Q31`) with `FPCR`/`FPSR`.
