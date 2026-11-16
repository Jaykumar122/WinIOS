# WinIOS Campaign 201 — Stage 201.7 & 201.8 MMU Differential Validation

**Campaign:** 201  
**Stages:** 201.7 (Stage-1 MMU) & 201.8 (Stage-2 & Combined Translation)  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target:** Independent validation of 16KB-granule Stage-1, Stage-2, and composite MMU translation pipelines  
**Date:** November 16, 2026  

---

## 1. Executive Summary

Stages 201.7 and 201.8 verified the Stage-1, Stage-2, and combined $VA \to IPA \to PA$ translation engines of WinIOS against the memory management architecture defined in *Arm DDI 0487K.a (Chapter D5: The AArch64 Virtual Memory System Architecture)*.

Key validation conclusions:
1. **16KB Granule Compliance:** Verified table alignment, granule selection via `TCR_EL1.TG0 = 0b10` and `VTCR_EL2.TG0 = 0b10`, and page offset indexing for 16,384-byte pages.
2. **Access Permission Integrity:** Exhaustively evaluated `AP[2:1]` permission combinations for unprivileged (EL0) and privileged (EL1) reads and writes.
3. **Access Flag & Fault Hierarchy:** Verified that descriptors with $AF = 0$ reliably trigger `AccessFlag` faults ($FSC = 0x08..0x0B$).
4. **Combined Permission Intersection:** Demonstrated that in a two-stage translation regime, Stage-2 permissions strictly constrain Stage-1 permissions:
   $$\text{Effective Access} = \text{Stage-1 Access} \cap \text{Stage-2 Access}$$
   A mapping configured as Read/Write in Stage-1 but Read-Only in Stage-2 correctly restricts guest write operations, triggering a Stage-2 Permission Fault.
5. **Architectural Fault Precedence:** Verified that translation faults occurring in Stage-1 abort the pipeline immediately before Stage-2 walk initiation, matching ARM DDI 0487 precedence rules.

---

## 2. Translation & Permission Differential Matrix

| Translation Regime | Test Case / Scenario | Arm DDI 0487 Rule | WinIOS Observed Behavior | Status |
| :--- | :--- | :--- | :--- | :--- |
| **Stage-1 Granule** | 16KB Granule (`TG0 = 0b10`) | Base must be 16KB aligned | Alignment check passes on `0x4000`, fails on `0x1000` | **PASS** |
| **Stage-1 AP=0b11** | Read-Only all ELs, EL0 read | Read permitted | Resolves physical address | **PASS** |
| **Stage-1 AP=0b11** | Read-Only all ELs, EL0 write | Write prohibited | `Permission` fault generated | **PASS** |
| **Stage-1 AP=0b11** | Read-Only all ELs, EL1 write | Write prohibited | `Permission` fault generated | **PASS** |
| **Stage-1 Access Flag** | Descriptor with $AF = 0$ | Access Flag fault before translation | Generates fault with $FSC = 0x08..0x0B$ | **PASS** |
| **Stage-1 UXN** | Unprivileged Execute-Never ($UXN=1$) | EL0 instruction fetch prohibited | `Permission` fault on instruction fetch | **PASS** |
| **Stage-2 Walk** | 16KB Granule Stage-2 walk | Resolves $IPA \to PA$ | Identical PA output | **PASS** |
| **Stage-2 S2AP** | S2AP = 0b01 (Read-Only) | Stage-2 write prohibited | `Permission` fault on write | **PASS** |
| **Combined Intersection** | S1 Read/Write $\cap$ S2 Read-Only | Resulting access is Read-Only | Read succeeds, Write faults at Stage-2 | **PASS** |
| **Fault Precedence** | Invalid S1 descriptor, S2 valid | S1 fault must be reported | Stage-1 `Translation` fault taken | **PASS** |

---

## 3. Test Suite Reference
- Executable 1: `stage201_7_mmu_stage1_differential_tests`
- Executable 2: `stage201_8_stage2_combined_differential_tests`
