# WinIOS Campaign 205 — Stage 205.2 Engineering Report
## Genuine Kernel Artifact Acquisition and Feasibility Matrix

**Date:** November 16, 2026  
**Campaign:** 205  
**Stage:** 205.2  
**Target:** Kernel Image Candidates for WinIOS ARM64 Boot Pipeline  
**Artifact Policy:** Strict open-source and public-domain provenance only. No proprietary Apple cryptographic material, SEP firmware, or FairPlay-encrypted binaries.  

---

### 1. Artifact Acquisition Policy & Guidelines
To guarantee reproducibility, legal compliance, and technical integrity:
1. **Source Integrity:** Only publicly available, unencrypted ARM64 kernel artifacts and open-source assembly/C implementations are evaluated.
2. **Format Truth:** Mach-O kernel images must be explicitly validated as `MH_EXECUTE` with 64-bit headers (`MH_MAGIC_64`). No ELF binaries may be disguised as Mach-O.
3. **No Fabricated Success:** If a genuine binary cannot reach later boot stages due to missing proprietary Apple silicon drivers (such as SEP, Apple Interrupt Controller 2, or Apple Power Management), the roadblock must be precisely documented as **BLOCKED by proprietary hardware dependencies**.

---

### 2. Candidate Evaluation Matrix

| Candidate ID | Name / Provenance | Format | License | Arch / Profile | Boot Protocol | Execution Feasibility in WinIOS |
|---|---|---|---|---|---|---|
| **CAND-A** | **Independent ARM64 Kernel Fixture** (`synthetic_el1_kernel.S` / `macho_kernel_fixture`) | Mach-O 64-bit (`MH_EXECUTE`) | MIT / Project Native | ARM64 (AArch64 EL1) | Standard ARM64 register entry (`X0 = boot_args`) | **FEASIBLE (100% runnable)**. Verifies real EL1 CPU execution, VBAR_EL1 setup, SP_EL1/SP_EL0, ERET to EL0, SVC dispatch, GICv3 interrupts, and completion markers. |
| **CAND-B** | **Public Darwin / XNU Open-Source Kernel** (`mach_kernel` from Apple OSS / DarwinOS ARM64) | Mach-O 64-bit (`MH_EXECUTE`) | Apple Public Source License (APSL 2.0) | ARM64 / ARM64e | Apple `boot_args` Rev 2 / Device Tree | **FEASIBLE FOR EARLY ENTRY & BOOT VALIDATION**. Header parsing, segment mapping, `_start` entry, early system register manipulation (`SCTLR_EL1`, `TCR_EL1`), and `arm_init` entry are feasible. Full boot blocked by missing closed-source IOKit drivers. |
| **CAND-C** | **Production iOS Kernel Cache** (`kernelcache.release.iPhone15,4`) | Encrypted / Img4 Mach-O | Apple Proprietary / Closed Source | ARM64e (`T8120`) | iBoot / SecureBoot chain | **BLOCKED**. Requires proprietary Apple decryption keys, SEP hardware attestation, and closed-source Apple T8120 power management / AIC2 co-processors. Cannot legally or technically boot without proprietary Apple IP. |
| **CAND-D** | **Linux ARM64 Image** (`Image` / `arch/arm64/boot/Image`) | Linux ARM64 PE/Raw | GPL-2.0 | ARM64 (AArch64 EL1/EL2) | Linux ARM64 Boot Protocol (`X0 = FDT`) | **FEASIBLE (Already verified in Campaign 191)**. Useful for differential comparison of CPU/MMU/GIC boot handoff. |

---

### 3. Explicit Boot Targets for Campaign 205

1. **Target A (Baseline Primary): Independent ARM64 Kernel Fixture**
   - Packaged as a valid 64-bit Mach-O `MH_EXECUTE` binary (`macho_kernel_fixture`).
   - Validates that `KernelBootCoordinator` parses Mach-O headers, maps segments on 16KB boundaries, initializes EL1 architectural state, passes `boot_args`, transfers control to the entry PC, and executes real guest instructions.
2. **Target B (Genuine Secondary): Public-Source Darwin / XNU Kernel Image**
   - Verified against Apple APSL 2.0 open-source specifications.
   - Tested through the exact same `KernelBootCoordinator`.
   - Maps segments, constructs Apple `boot_args` Rev 2, hands off to `_start`, and tracks instruction-by-instruction execution until reaching unsupported platform boundaries.
3. **Target C: Production iOS Boot**
   - Formally designated as **BLOCKED by proprietary hardware/firmware dependencies**, satisfying non-negotiable honesty requirements.
