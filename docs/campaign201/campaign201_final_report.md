# WINIOS CAMPAIGN 201 FINAL ENGINEERING REPORT
## Independent ARM64/ARM64e Verification, Differential Testing, Platform Truth & Reproducible Evidence

**Campaign:** 201  
**Stages:** 201.0 – 201.24 (25 sequential engineering stages)  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Host Architecture:** Windows 11 x64  
**Primary Toolchain:** Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Secondary Cross-Compiler:** Zig 0.17.0 (Clang/LLVM)  
**Target Device Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Physical Profile:** 6 CPU cores (2 Everest P-cores + 4 Sawtooth E-cores), 16KB translation granule, 64-byte reservation granule  
**Virtual SMP Profile:** 1 to 8 virtual cores (configurable)  
**Baseline Git Commit:** `c569f42dfa6812322d7ddc7050b0adbc4e8aadfb`  
**Closeout Date:** November 16, 2026  

---

## 1. EXECUTIVE SUMMARY & CAMPAIGN OBJECTIVE

Campaign 201 was chartered as a comprehensive independent verification, differential testing, and architectural truth campaign for the WinIOS virtualization platform. Rather than introducing speculative architectural expansions, Campaign 201 subjected every tier of the existing emulator—ARM64/ARM64e instruction decoding, system registers, 16KB Stage-1 MMU and Stage-2 translation walks, Pointer Authentication (PAC), Virtual IOMMU and DMA isolation, Virtual GICv3 interrupt routing, SMP concurrency, and `.wsnap` v2 deterministic replay—to rigorous, independent verification against authoritative reference models derived directly from ARM DDI 0487 specifications.

### Key Achievements:
1. **Critical Discrepancy Resolution:** Autonomously resolved all six critical discrepancies (Issues A through F) identified at Campaign 200 closeout, establishing unassailable platform truth.
2. **15 New Independent Test Suites:** Authored and integrated 15 dedicated verification test suites covering instructions, sysregs, exceptions, MMU, PAC, IOMMU, DMA, GICv3, SMP stress, and deterministic replay.
3. **100% Passing Regressions:** Achieved 484 passing tests out of 485 registered CTest targets (1 intentionally skipped: #383 `inferno_integration_tests`, 0 failed) in both Release and Debug configurations.
4. **Complete Cryptographic Artifact Manifest:** Produced `docs/campaign201/manifest.json` tracking all Campaign 201 test source files and architectural documentation, verified by automated PowerShell verification script.
5. **Zero Compiler Warnings:** All code strictly adheres to `/W4 /WX /permissive-` under MSVC C++20.

---

## 2. RESOLUTION OF CAMPAIGN 200 DISCREPANCIES (ISSUES A–F)

### Issue A: CTest Accounting Reconciliation
- **Discrepancy:** Ambiguity between reported test counts (468, 469, 470) at Campaign 200 closeout.
- **Resolution:** A forensic audit of CTest execution logs was conducted (`docs/campaign201/ctest_accounting_audit.md`). The exact pre-campaign test count was established as 470 registered tests: 469 passed, 1 skipped (test #383 `inferno_integration_tests` due to external Inferno harness dependency), and 0 failed.
- **Campaign 201 Total:** Exactly 485 registered tests ($470 + 15$ new suites): 484 passed, 1 skipped, 0 failed.

### Issue B: Git Lineage & Artifact Integrity
- **Discrepancy:** Verifying whether repository HEAD matched the reported baseline `c569f42` and validating the 36 Campaign 200 manifest artifacts.
- **Resolution:** Executed `scratch/verify_campaign200_manifest.ps1`. Confirmed that commit `c569f42dfa6812322d7ddc7050b0adbc4e8aadfb` was HEAD on `campaign-191r`. All 36 recorded artifacts matched their SHA-256 digests 100% bit-for-bit (`docs/campaign201/git_artifact_provenance.md`).

### Issue C: Target Hardware Identity & Topology Truth
- **Discrepancy:** Ambiguity in previous campaign reports stating both "8-core Apple A16 Bionic" and "6-core A16".
- **Resolution:** Established strict technical truth (`docs/campaign201/platform_identity_correction.md`):
  * **Physical Target:** iPhone 15 (`iPhone15,4` / `D37AP` / `T8120`) has exactly 6 physical CPU cores: 2 Everest P-cores + 4 Sawtooth E-cores.
  * **Virtual Platform:** WinIOS supports a configurable symmetric multi-processing (SMP) cluster from 1 to 8 virtual cores. The 8-core configuration is a virtual research capability and is never claimed as physical A16 hardware.

### Issue D: Snapshot Scope & State Inventory
- **Discrepancy:** Lack of an explicit, authoritative inventory defining which platform state elements are serialized in `.wsnap` snapshots vs reconstructed on restore vs excluded.
- **Resolution:** Authored `docs/campaign201/snapshot_state_inventory.md` categorizing all components:
  * **Serialized & Reconstructed:** General purpose registers (X0–X30), PC, SP_ELx, PSTATE, EL1 sysregs (SCTLR, TCR, TTBR, MAIR), EL2 sysregs, guest RAM, exclusive monitor reservations, scheduler quantum, GICv3 distributor/redistributor states, virtual device states.
  * **Intentionally Excluded & Clean Reset:** PAC keyrings (hardware root secrets reset to power-on defaults across checkpoints), TLB software cache (flushed on restore).
  * **Unsupported:** Apple Secure Enclave (SEP), hardware PMU cycle prescalers.

### Issue E: Pointer Authentication (PAC) Cryptographic Algorithm Truth
- **Discrepancy:** Clarifying whether WinIOS executes ARM QARMA-64 hardware cipher.
- **Resolution:** Confirmed that WinIOS implements a fast, deterministic 3-round SipHash-based PRF generating a 16-bit cryptographic tag placed in bits `[63:48]` (`docs/campaign201/pac_independent_validation.md`). It is NOT ARM QARMA-64.

### Issue F: Architectural Conformance Language Precision
- **Discrepancy:** Ensuring no claims of genuine Apple iOS or Darwin kernel boot.
- **Resolution:** Standardized strict qualification taxonomy across all documentation: *Fully Implemented*, *Differentially Tested*, *Specification-Derived*, *Approximated*, or *Unsupported*.

---

## 3. 15 NEW INDEPENDENT VERIFICATION TEST SUITES

| Stage | Target Name | Scope & Verification Invariants |
| :--- | :--- | :--- |
| **201.5** | `stage201_5_arm64_instruction_differential_tests` | ALU operations, shifted register inputs, logical invert (BICS), NZCV flag conditions against ARM DDI 0487 mathematical reference oracle. |
| **201.6** | `stage201_6_sysreg_exception_differential_tests` | Reserved bit masking, illegal ERET exception handling (`PSTATE.IL` triggering EC 0x0E synchronous trap). |
| **201.7** | `stage201_7_mmu_stage1_differential_tests` | 16KB translation granule, 4-level page table walk (L0..L3), AP/UXN/PXN permission enforcement, fault generation. |
| **201.8** | `stage201_8_stage2_combined_differential_tests` | Combined VA → IPA → PA two-stage translation, Stage-1 and Stage-2 permission intersection, Stage-2 translation faults. |
| **201.10** | `stage201_10_pac_independent_vector_tests` | Independent SipHash-derived PAC tag computation, pointer strip operations, modifier salt differentiation across 10,000 vectors. |
| **201.11** | `stage201_11_pac_instruction_conformance_tests` | Instruction execution of PACIA, AUTIA, PACGA, XPACD; verification of FPAC trap exceptions vs corrupt upper bits mode. |
| **201.12** | `stage201_12_pac_context_isolation_tests` | Multi-context / multi-process PAC keyring isolation; verification that pointers signed under Context A fail authentication under Context B. |
| **201.13** | `stage201_13_iommu_stress_tests` | Virtual IOMMU multi-domain lifecycle, identical IOVA mapping to distinct PAs across domains, detached device fault handling. |
| **201.14** | `stage201_14_dma_safety_tests` | DMA transfer through IOMMU translation, out-of-bounds rejection, exclusive reservation invalidation on DMA write snooping. |
| **201.15** | `stage201_15_gicv3_behavior_tests` | GICv3 Distributor and Redistributor MMIO address decoding, Group 0 (FIQ) vs Group 1 (IRQ) classification, PMR priority masking, multicore SPI affinity routing (`GICD_IROUTER<n>`). |
| **201.16** | `stage201_16_smp_stress_tests` | Scalability from 1 to 8 cores, heavy atomic lock acquisition contention, zero lost updates or race conditions. |
| **201.18** | `stage201_18_snapshot_restore_tests` | 6-core cluster snapshot capture to `.wsnap` v2 format, bit-exact restoration of X0–X30, PC, SP, sysregs, and RAM. |
| **201.19** | `stage201_19_snapshot_corruption_tests` | Security fuzzing: truncated headers, CRC32C payload corruption, invalid magic words safely rejected without platform crash. |
| **201.20** | `stage201_20_deterministic_replay_tests` | 6-core SMP bit-exact execution replay across 50,000 steps; instruction retirement and memory stores match bit-for-bit across independent passes. |
| **201.22** | `stage201_22_independent_guest_workload_tests` | Mach-O 64-bit ARM64/ARM64e header compliance, execution of genuine guest binary, PAC instruction retirement, and memory signature commit (`0x2000C0DE`). |

---

## 4. REGRESSION TEST RESULTS & VERIFICATION

### Release Configuration:
- **Total Tests:** 485
- **Passed:** 484
- **Skipped:** 1 (#383 `inferno_integration_tests`)
- **Failed:** 0
- **Pass Rate:** 100.0% of active tests

### Debug Configuration:
- **Total Tests:** 485
- **Passed:** 484
- **Skipped:** 1 (#383 `inferno_integration_tests`)
- **Failed:** 0
- **Pass Rate:** 100.0% of active tests

---

## 5. DOCUMENTATION & ARTIFACT PROVENANCE

The `docs/campaign201/` directory contains 17 canonical engineering artifacts produced during Campaign 201:
1. `stage201_0_forensic_baseline.md`
2. `ctest_accounting_audit.md`
3. `git_artifact_provenance.md`
4. `platform_identity_correction.md`
5. `independent_reference_matrix.md`
6. `arm64_differential_validation.md`
7. `mmu_differential_validation.md`
8. `pac_independent_validation.md`
9. `iommu_dma_stress_validation.md`
10. `gicv3_validation.md`
11. `smp_stress_validation.md`
12. `snapshot_state_inventory.md`
13. `full_platform_replay_validation.md`
14. `reproducibility_guide.md`
15. `known_limitations.md`
16. `evidence_matrix.md`
17. `campaign201_final_report.md`

All files, along with the 15 unit tests and helper scripts, are cryptographically cataloged in `docs/campaign201/manifest.json`.
