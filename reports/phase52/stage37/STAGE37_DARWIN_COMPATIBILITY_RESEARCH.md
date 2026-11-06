# Stage 37 Research Report: Darwin System Register Compatibility Layer

**Project:** WinIOS  
**Phase:** 52  
**Stage:** 37  
**Author:** Platform Compatibility & Research Agent  
**Date:** 2026-11-06  
**Status:** PASS  

---

## 1. Executive Summary

This report establishes the technical basis for Apple Silicon / Darwin system register compatibility research in WinIOS. 

Stage 37 explicitly isolates all Darwin-related register modeling from the active CPU instruction loop. The purpose of this layer is architectural research, classification, and compatibility scaffolding—**not** active iOS kernel booting or ad-hoc stubbing.

---

## 2. Register Inventory and Evidence Classifications

A curated registry of 20 system registers has been defined in `src/cpu/darwin_sysreg_compat.h` and `src/cpu/darwin_sysreg_compat.cpp`, classified into four formal evidence categories:

### 2.1 Generic Architectural (ARMv8 Standard)
Registers that are part of the standard ARMv8-A architecture and utilized identically by Darwin/XNU:
1. `CPACR_EL1` (`0x4082`, `s3_0_c1_c0_2`): Coprocessor access control; controls FP/SIMD traps.
2. `SCTLR_EL1` (`0x4080`, `s3_0_c1_c0_0`): System control register (MMU, caches, alignment).
3. `TCR_EL1` (`0x4102`, `s3_0_c2_c0_2`): Translation control register (granules, T0SZ, T1SZ).
4. `TTBR0_EL1` (`0x4100`, `s3_0_c2_c0_0`): Translation table base register 0 (user address space).
5. `TTBR1_EL1` (`0x4101`, `s3_0_c2_c0_1`): Translation table base register 1 (kernel address space).
6. `MAIR_EL1` (`0x4510`, `s3_0_c10_c2_0`): Memory attribute indirection register.
7. `VBAR_EL1` (`0x4600`, `s3_0_c12_c0_0`): Vector base address register.
8. `CNTVCT_EL0` (`0x5E02`, `s3_3_c14_c0_2`): Virtual cycle counter.

### 2.2 Publicly Documented Darwin (Open-Source XNU)
Registers with specific semantics documented in open-source Apple XNU kernel releases:
9. `TPIDR_EL0` (`0x5E82`, `s3_3_c13_c0_2`): User TLS thread pointer, exposed to userland pthread library.
10. `TPIDRRO_EL0` (`0x5E83`, `s3_3_c13_c0_3`): User read-only thread pointer; XNU stores the CPU number and thread pointer here.
11. `TPIDR_EL1` (`0x4684`, `s3_0_c13_c0_4`): Kernel thread pointer; XNU points this to `current_thread()`.

### 2.3 Research-Only (Apple Silicon Custom Implementations)
Non-standard implementation registers reverse-engineered by the community (e.g., Asahi Linux, Corellium) and documented in security research papers:
12. `APSTS_EL1` (`0x4090`, `s3_0_c15_c1_0`): Apple Performance and Status register.
13. `ACC_OVRD_EL1` (`0x4091`, `s3_0_c15_c6_0`): Apple Core Control Override register (cache behaviors, branch predictors).
14. `SPRR_CONFIG_EL1` (`0x4092`, `s3_0_c15_c1_5`): Stage Permission Restriction Register (Apple Page Protection Layer).
15. `GXF_STATUS_EL1` (`0x4093`, `s3_0_c15_c8_0`): Guarded Execution Framework (sub-EL privilege domain switching).
16. `M1_PMCR_EL0` (`0x5F00`, `s3_3_c15_c0_0`): Apple Silicon proprietary performance monitoring control.
17. `HID0_EL1` (`0x4094`, `s3_0_c15_c0_0`): Hardware Implementation Dependent 0.
18. `HID1_EL1` (`0x4095`, `s3_0_c15_c0_1`): Hardware Implementation Dependent 1.

### 2.4 Unsupported / Architectural Extensions
Registers known to exist in advanced ARMv8/v9 revisions but strictly rejected in the current WinIOS baseline:
19. `ZCR_EL1` (`0x4083`, `s3_0_c1_c2_0`): Scalable Vector Extension (SVE) control. Rejected.
20. `SVCR` (`0x4084`, `s3_0_c4_c2_2`): Scalable Matrix Extension (SME) vector control. Rejected.

---

## 3. Strict Execution Isolation Guardrails

To prevent unverified registers from destabilizing deterministic replay or introducing fake boot semantics:
1. **No Guest Opcode Dispatch:** The CPU main decoding loop in `cpu.cpp` queries `SysRegRegistry` and `sysreg.cpp`. It **never** dispatches against `DarwinCompatibilityRegistry`.
2. **Profile Configuration Hash:** `DarwinProfile` exposes an immutable configuration hash (`0x71661cb5`) which ensures that any experimental enabling of profile registers is recorded deterministically in snapshot and SNDL logs.
3. **Explicit Failure Policy:** Any attempted access to `ResearchOnly` or `Unsupported` registers by guest code triggers a deterministic `FpSimdAccessTrap` or `UndefinedInstruction` exception.
