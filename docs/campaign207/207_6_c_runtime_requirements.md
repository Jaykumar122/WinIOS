# Stage 207.6: C Runtime Initialization Requirements

## 1. Executive Summary
- **Stage:** 207.6 — C Runtime Initialization Requirements
- **Status:** COMPLETE / VERIFIED
- **Date:** 2026-11-16
- **Architecture:** ARM64 (AArch64) / AAPCS64 LP64 ABI

---

## 2. Mandatory C Runtime Invariants for XNU Early Boot

1. **Stack Pointer (SP_EL1):**
   - Must be aligned to a 16-byte boundary at all times (`SP % 16 == 0`).
   - Must point to writable, physical DRAM that does not overlap with the kernel text, data, device tree, or boot arguments.
   - Initialized to `bootstrap_stack_top` in `xnu_start.s`.

2. **Calling Convention (AAPCS64):**
   - First argument (`boot_args*`) passed in register `X0`.
   - Callee-saved registers (`X19`–`X28`) preserved across calls.
   - Frame Pointer (`X29`) and Link Register (`X30`) saved on stack entry:
     ```arm64
     stp x29, x30, [sp, #-16]!
     mov x29, sp
     ```
     and restored on exit:
     ```arm64
     ldp x29, x30, [sp], #16
     ret
     ```

3. **Floating Point and SIMD (NEON):**
   - In AArch64, modern compilers (Clang/LLVM) may emit vector or FP registers (`Q0`..`Q31` or `D0`..`D31`) for memory operations (`memset`, struct copy).
   - `CPACR_EL1.FPEN` must be initialized to `0b11` (`0x300000`) before entering compiled C code to prevent synchronous `EC = 0x07` traps.

4. **BSS and Global Data Initialization:**
   - Static global variables defined with zero-initialization (`volatile platform_state_t g_platform_state = {0};`) must reside in zeroed memory.
   - For freestanding kernel execution, `xnu_start.s` or the loader clears the BSS segment (`__DATA.__bss`) before invoking `arm_init`.

5. **Position Independence and Relocations:**
   - Free-standing early kernel code compiled with `-ffreestanding -fno-stack-protector -fno-sanitize=all -O2` uses PC-relative addressing (`ADRP` + `ADD`/`LDR`) for data and functions.
   - Avoids unresolved dynamic linker relocations prior to VM enablement.

6. **Memory Barriers & Architectural Serialization:**
   - `DSB ISH` (Data Synchronization Barrier, Inner Shareable) ensures memory stores to page tables and mailbox are architecturally visible.
   - `ISB` (Instruction Synchronization Barrier) serializes system register writes (`MSR VBAR_EL1`, `MSR TCR_EL1`, `MSR MAIR_EL1`, `MSR TTBR0/1_EL1`).
