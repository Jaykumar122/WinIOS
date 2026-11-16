# WinIOS Campaign 201 — Architectural Evidence & Verification Matrix

**Campaign:** 201  
**Stages:** 201.0 – 201.24  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Architecture:** Windows 11 x64  
**Date:** November 16, 2026  
**Status:** Canonical Reference Matrix  

---

## 1. Verification Strategy Overview

Campaign 201 enforces rigorous, independent verification across all core subsystems of the WinIOS virtualization platform:
1. **Differential CPU & Sysreg Testing:** Independent reference models deriving expected states directly from ARM DDI 0487 architecture specifications.
2. **Translation Subsystems:** 4-level 16KB Stage-1 MMU and Stage-2 combined address translation walks.
3. **ARM64e Pointer Authentication:** Verification of 3-round SipHash PRF tag generation, instruction stripping, FPAC trap exceptions, and process keyring isolation.
4. **Virtual IOMMU & DMA Safety:** Domain-isolated address translation, out-of-bounds boundary checks, and cache-coherent reservation snooping.
5. **Virtual GICv3 Interrupt Architecture:** Distributor and Redistributor memory-mapped register decoding, Group 0/1 interrupt classification, PMR priority masking, and multicore affinity routing.
6. **Multi-Core Concurrency & Replay:** 1-to-8 core SMP scaling, exclusive store contention, bit-exact deterministic replay, and CRC32C `.wsnap` v2 snapshot restore integrity.
7. **Genuine Guest Workload:** Execution of Darwin ARM64/ARM64e Mach-O binary compiled for Apple A16 target.

---

## 2. Comprehensive Evidence Matrix

| Stage | Feature / Subsystem | Test Target Name | Implementation / Source Files | Verification Method | Invariant Enforced | Result |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **201.0** | Forensic Baseline Audit | N/A (Diagnostic) | `docs/campaign201/stage201_0_forensic_baseline.md` | Static Analysis & Git Provenance | Resolved Issues A–F, audited CTest accounting (470 total) | **PASS** |
| **201.1** | CTest Accounting Audit | `audit_ctest_accounting.ps1` | `docs/campaign201/ctest_accounting_audit.md` | Automated CTest Run & Log Parsing | Standardized 470 = 469 passed + 1 skipped + 0 failed | **PASS** |
| **201.2** | Git Lineage Audit | `verify_campaign200_manifest.ps1`| `docs/campaign201/git_artifact_provenance.md` | SHA-256 Digest Verification | All 36 Campaign 200 artifacts verified bit-for-bit | **PASS** |
| **201.3** | Hardware Identity Audit | N/A (Documentation) | `docs/campaign201/platform_identity_correction.md` | Specification Audit | Physical A16 (6 cores) decoupled from virtual SMP (1..8 cores) | **PASS** |
| **201.4** | Reference Oracles | N/A (Architecture) | `docs/campaign201/independent_reference_matrix.md`| ARM DDI 0487 Derivations | Mathematical models for ALU, NZCV, PAC, and MMU | **PASS** |
| **201.5** | ARM64 Instruction Conformance | `stage201_5_arm64_instruction_differential_tests` | `tests/unit/stage201_5_arm64_instruction_differential_tests.cpp` | Differential (Reference vs CPU) | Shifted ALU, Logical Invert, NZCV flag conditions | **PASS** |
| **201.6** | System Registers & Exceptions | `stage201_6_sysreg_exception_differential_tests` | `tests/unit/stage201_6_sysreg_exception_differential_tests.cpp` | Exception Delivery Validation | Sysreg masking, illegal ERET PSTATE.IL trap (EC 0x0E) | **PASS** |
| **201.7** | Stage-1 MMU Differential | `stage201_7_mmu_stage1_differential_tests` | `tests/unit/stage201_7_mmu_stage1_differential_tests.cpp` | Differential Page Walker | 16KB granule, 4-level walk (L0..L3), AP/UXN/PXN permissions | **PASS** |
| **201.8** | Stage-2 Combined Translation | `stage201_8_stage2_combined_differential_tests` | `tests/unit/stage201_8_stage2_combined_differential_tests.cpp` | Combined Walk Validation | VA → IPA → PA two-stage resolution, permission intersection | **PASS** |
| **201.9** | PAC Reference Implementation | `docs/campaign201/pac_independent_validation.md` | `src/cpu/pointer_auth.cpp`, `include/cpu/pointer_auth.h` | Algorithmic Audit | Truth disclosure: 3-round SipHash PRF (not QARMA-64) | **PASS** |
| **201.10**| PAC Independent Vectors | `stage201_10_pac_independent_vector_tests` | `tests/unit/stage201_10_pac_independent_vector_tests.cpp` | Cryptographic Oracle | Top 16-bit tag insertion, stripping, modifier differentiation | **PASS** |
| **201.11**| PAC Instruction Conformance | `stage201_11_pac_instruction_conformance_tests` | `tests/unit/stage201_11_pac_instruction_conformance_tests.cpp` | Architectural Instruction Test | PACIA, AUTIA, PACGA, FPAC synchronous exception traps | **PASS** |
| **201.12**| PAC Context Isolation | `stage201_12_pac_context_isolation_tests` | `tests/unit/stage201_12_pac_context_isolation_tests.cpp` | Cross-Process Validation | Distinct keyrings prevent cross-process pointer forgery | **PASS** |
| **201.13**| Virtual IOMMU Stress | `stage201_13_iommu_stress_tests` | `tests/unit/stage201_13_iommu_stress_tests.cpp` | Multi-Domain Stress Test | High-volume domain creation, overlapping IOVAs, fault traps | **PASS** |
| **201.14**| DMA Safety & Coherency | `stage201_14_dma_safety_tests` | `tests/unit/stage201_14_dma_safety_tests.cpp` | Coherency & Monitor Validation | DMA through IOMMU, exclusive monitor invalidation on DMA write | **PASS** |
| **201.15**| Virtual GICv3 Routing | `stage201_15_gicv3_behavior_tests` | `tests/unit/stage201_15_gicv3_behavior_tests.cpp` | Multicore Interrupt Test | GICD/GICR address decoding, Group 0/1, PMR, SPI affinity | **PASS** |
| **201.16**| SMP Concurrency Stress | `stage201_16_smp_stress_tests` | `tests/unit/stage201_16_smp_stress_tests.cpp` | Concurrency & Contention | 1..8 core scaling, atomic lock acquisition, zero race conditions | **PASS** |
| **201.17**| Snapshot State Inventory | N/A (Forensic Catalog) | `docs/campaign201/snapshot_state_inventory.md` | Architectural Source Audit | Catalog of serialized vs reconstructed vs excluded elements | **PASS** |
| **201.18**| Complete Snapshot Restore | `stage201_18_snapshot_restore_tests` | `tests/unit/stage201_18_snapshot_restore_tests.cpp` | Cluster Snapshot/Restore | 6-core cluster restoration: X0-X30, PC, SP, sysregs, RAM | **PASS** |
| **201.19**| Snapshot Corruption & Security | `stage201_19_snapshot_corruption_tests` | `tests/unit/stage201_19_snapshot_corruption_tests.cpp` | Malformed Payload Fuzzing | Truncation, CRC32C tampering, invalid magic rejected safely | **PASS** |
| **201.20**| Deterministic Replay | `stage201_20_deterministic_replay_tests` | `tests/unit/stage201_20_deterministic_replay_tests.cpp` | Multi-Pass Execution | 6-core SMP bit-exact execution replay across 50,000 steps | **PASS** |
| **201.21**| Full Platform Replay Audit | N/A (System Audit) | `docs/campaign201/full_platform_replay_validation.md` | Cross-Subsystem Trace Audit | Invariant preservation across snapshot/replay boundaries | **PASS** |
| **201.22**| Guest Workload Execution | `stage201_22_independent_guest_workload_tests` | `tests/unit/stage201_22_independent_guest_workload_tests.cpp` | Mach-O Execution Test | Mach-O loading, PAC instruction retirement, 0x2000C0DE commit | **PASS** |
| **201.23**| Known Limitations Disclosure | N/A (Reference Document) | `docs/campaign201/known_limitations.md` | Technical Truth Audit | Full disclosure of architectural boundaries and approximations | **PASS** |
| **201.24**| Final Report & Closeout | N/A (Campaign Closeout) | `docs/campaign201/manifest.json`, Final Report | Release & Debug Regressions | 485 registered tests: 484 passed, 1 skipped, 0 failed | **PASS** |

---

## 3. Regression Test Accounting Summary

| Suite / Category | Pre-Campaign 201 | Added in Campaign 201 | Total Registered | Passed | Skipped | Failed |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Core Architecture & CPU** | 124 | 2 (`stage201_5`, `stage201_6`) | 126 | 126 | 0 | 0 |
| **MMU & Translation** | 58 | 2 (`stage201_7`, `stage201_8`) | 60 | 60 | 0 | 0 |
| **Pointer Authentication (PAC)** | 32 | 3 (`stage201_10`, `11`, `12`) | 35 | 35 | 0 | 0 |
| **IOMMU & DMA Safety** | 24 | 2 (`stage201_13`, `stage201_14`) | 26 | 26 | 0 | 0 |
| **Interrupt Controller (GICv3)** | 18 | 1 (`stage201_15`) | 19 | 19 | 0 | 0 |
| **SMP Concurrency & Scheduling** | 62 | 1 (`stage201_16`) | 63 | 63 | 0 | 0 |
| **Snapshot & Deterministic Replay**| 46 | 3 (`stage201_18`, `19`, `20`) | 49 | 49 | 0 | 0 |
| **Guest Workloads & Mach-O** | 44 | 1 (`stage201_22`) | 45 | 45 | 0 | 0 |
| **Inferno Integration Suite** | 1 | 0 | 1 | 0 | 1 (skipped) | 0 |
| **Historical & Integration Tests** | 61 | 0 | 61 | 61 | 0 | 0 |
| **TOTALS** | **470** | **15** | **485** | **484** | **1** | **0** |
