# Stage 36 Limitations & Boundaries: System Register Architecture & Execution Conformance

**Project:** WinIOS  
**Phase:** 52  
**Stage:** 36  
**Date:** 2026-11-06  
**Status:** ACTIVE  

---

## 1. Explicit Architectural Limitations

While Stage 36 successfully establishes a centralized system register registry, complete thread-pointer registers (`TPIDR_EL0`, `TPIDR_EL1`, `TPIDRRO_EL0`), strict EL0 privilege enforcement, and 1080-byte snapshot compatibility, the following architectural boundaries remain in effect:

### 1.1 Unimplemented System Registers
- **EL2 / EL3 Registers:** While descriptors exist in `sysreg.cpp` for Hypervisor (EL2) and Secure Monitor (EL3) registers (e.g. `HCR_EL2`, `VTTBR_EL2`, `SCR_EL3`), full virtualization and EL2/EL3 trap routing remain stubbed. Attempting to switch to EL2/EL3 or execute hypervisor virtualization instructions will trigger an undefined instruction or illegal execution state exception.
- **Advanced Debug & Trace:** CoreSight trace registers, statistical profiling extension (SPE), and performance monitor counters beyond cycle/event counters are descriptors only and do not stream physical trace packets.
- **Cryptographic & Vector Extensions:** SVE (Scalable Vector Extension) and SME (Scalable Matrix Extension) system registers (`ZCR_EL1`, `SVCR`, etc.) are not implemented; NEON 128-bit SIMD registers V0–V31 remain the authoritative vector model.

### 1.2 Privilege & Exception Trapping Boundaries
- **Trap Redirection:** System register access traps currently vector to EL1 with `EC=0x00` (Undefined Instruction). Architectural fine-grained traps (e.g., `CPACR_EL1.FPEN` trapping FP/SIMD to EL1 with `EC=0x07`, or `SCR_EL3.TRM` trapping to EL3) are only partially implemented.
- **AArch32 Execution State:** WinIOS remains strictly an AArch64-only execution engine. Any attempt to return via ERET to an AArch32 state (`SPSR.M[4] == 1`) is rejected as an illegal execution state exception (`EC=0x0E`), which is compliant with an AArch64-only implementation.

### 1.3 Memory System & TLB Invalidation
- **TLB Maintenance Instructions:** Instructions such as `TLBI VMALLE1IS` or `TLBI VAE1IS` are accepted and clear internal software TLB cache structures, but multi-level hardware page walks rely on the linear virtual memory paging model implemented in Stage 35.
- **Cache Clean/Invalidate Instructions:** Instructions like `DC CIVAC`, `DC ZVA`, `IC IVAU` operate deterministically in memory, but physical L1/L2/L3 cache coherence simulation is not modeled.

### 1.4 iOS Boot Capability
- **Non-Bootable Status:** WinIOS does NOT yet boot a full Darwin/XNU kernel or iOS userspace environment. While low-level CPU architectural conformance and thread-pointer semantics are established, full peripheral virtualization (AIC/VIC interrupt controller, Apple Silicon specific MMIO, Secure Enclave) is outside the scope of Phase 52.

---

## 2. Deferred Items for Stage 37 & Beyond

1. Fine-grained floating-point and SIMD access trap configuration via `CPACR_EL1`.
2. Enhanced MMU Translation Table Walk caching and page table fault granularity.
3. Apple-specific system registers (SPR / HID registers used by Darwin kernels).
4. Full PSCI v1.1 CPU_SUSPEND and SYSTEM_OFF handling across all SMP topologies.
