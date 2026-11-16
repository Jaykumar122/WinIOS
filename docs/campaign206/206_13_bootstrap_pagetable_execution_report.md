# Stage 206.13: Bootstrap Page-Table Execution Report

## 1. Executive Summary
- **Stage:** 206.13 — Bootstrap Page-Table Execution
- **Status:** PASS
- **Date:** 2026-11-16
- **Subsystem:** ARM64 16KB Granule Stage-1 Translation (`TTBR0_EL1` / `TTBR1_EL1`)
- **Key Verifications:** Dual-root identity + higher-half page table walks, 16KB block descriptors, permission fault generation on W^X violations.

---

## 2. Page Table Structure & Register Configurations

```
Register    Configuration Value             Architectural Meaning
TCR_EL1     (1<<30)|(2<<14)|(25<<16)|25     TG0=16KB (0b10), TG1=16KB (0b01), T0SZ=25 (48-bit), T1SZ=25 (48-bit)
TTBR0_EL1   0x0000000000020000              Identity mapping root table for low virtual addresses (0x0..0x3FFFFF)
TTBR1_EL1   0x0000000000024000              Kernel higher-half virtual address mapping root table
MAIR_EL1    0x0000000444FF0400              Index 0: Device-nGnRnE (0x00), Index 1: Normal Outer/Inner WBWA (0xFF)
```

---

## 3. Translation Test Outcomes
1. **Identity Mapping Translation (`TTBR0_EL1`):** Verified translation of `VA 0x1000` to `PA 0x1000` with EL1 Read/Write permissions.
2. **Higher-Half Mapping Translation (`TTBR1_EL1`):** Verified translation of `VA 0xFFFFFE0000004000` to physical offset `0x4000`.
3. **Permission Enforcement:** Attempting write to page marked `AP = 0b10` (Read-Only) triggers FSC `0x0D` (Permission Fault Level 1).
4. **Unmapped Range Fault:** Accessing unmapped virtual space outside translation tables triggers FSC `0x05` (Translation Fault Level 1).
