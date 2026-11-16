# Stage 206.17: Early CPU Initialization Report

## 1. Executive Summary
- **Stage:** 206.17 — Early CPU Initialization
- **Status:** PASS
- **Date:** 2026-11-16
- **Subsystem:** ARM64 Architectural Boot State Configuration (`Arm64CpuBootInitializer`, `src/cpu/arm64_cpu_boot_state.h/.cpp`)

---

## 2. Boot State & System Register Checklist

| Architectural Item | Target Configuration | Actual State | Conformance Status |
|---|---|---|---|
| Privilege Level | EL1 (Non-secure kernel mode) | `CurrentEL = 0b01` | CONFIRMED |
| Stack Selection | SP_EL1 (`SPSel = 1`) | `SPSel = 1` (EL1h) | CONFIRMED |
| Program Counter | `initial_pc` (Mach-O Entry Point) | `PC = 0x1000` | CONFIRMED |
| Stack Pointer | 16KB aligned DRAM address | `SP = 0x80000` | CONFIRMED |
| Argument X0 | Pointer to `AppleBootArgsRev2` | `X0 = 0x20000` | CONFIRMED |
| Saved Arg X19 | Pointer to `AppleBootArgsRev2` | `X19 = 0x20000` | CONFIRMED |
| `VBAR_EL1` | Exception vector base | `0x4000` (2KB aligned) | CONFIRMED |
| `TCR_EL1` | 16KB granule, 48-bit address space | `(1<<30)\|(2<<14)\|(25<<16)\|25` | CONFIRMED |
| `TTBR0_EL1` | Base of identity translation table | `0x20000` | CONFIRMED |
| `TTBR1_EL1` | Base of kernel translation table | `0x24000` | CONFIRMED |
