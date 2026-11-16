# WinIOS Campaign 203 — Stage 203.2 Verification Report
## EL2 Syndrome & HPFAR Architectural Audit

**Campaign:** 203  
**Stage:** 203.2  
**Timestamp:** November 16, 2026  
**Repository:** `D:\WinISO26\WinIOS\`  
**Test Executable:** `stage203_2_el2_syndrome_hpfar_audit_tests`  
**Configurations Tested:** Release & Debug (MSVC C++20 `/W4 /WX /permissive-`)  
**Status:** **PASS** (100% passing tests)  

---

## 1. Architectural Audit Objectives & Findings

Stage 203.2 conducted an independent architectural audit of the EL2 Stage-2 fault and syndrome reporting subsystem against ARM DDI 0487 specifications.

### Key Architectural Verifications:
1. **`ESR_EL2` Layout & Exception Classes:**
   - Instruction Abort taken to EL2: `EC = 0b100000` (`0x20`), `IL = 1` (bit 25).
   - Data Abort taken to EL2: `EC = 0b100100` (`0x24`), `IL = 1` (bit 25).
   - `WnR` (Write-not-Read) flag in bit 6: set to 1 for write accesses, 0 for read/instruction accesses.
   - Fault Status Codes (FSC) in bits `[5:0]`:
     - Translation faults: `0x04` (L0), `0x05` (L1), `0x06` (L2), `0x07` (L3).
     - Access flag faults: `0x08` (L0), `0x09` (L1), `0x0A` (L2), `0x0B` (L3).
     - Permission faults: `0x0C` (L0), `0x0D` (L1), `0x0E` (L2), `0x0F` (L3).
   - Verified against hardcoded mathematical reference oracles without relying on implementation calculations.

2. **`HPFAR_EL2` Field Layout & Defect Discovery:**
   - **Specification:** ARM DDI 0487 defines `HPFAR_EL2[39:4]` as holding bits `[47:12]` of the faulting Intermediate Physical Address (FIPA), with bits `[3:0]` and bits `[63:40]` defined as RES0.
   - **Defect Identified & Remediated:** The Campaign 202 baseline code applied mask `0x00000FFFFFFFF000ULL`, which erroneously cleared bits `[11:4]` (wiping out `FIPA[19:12]`). The mask was corrected to `0x000000FFFFFFFFF0ULL` (36 bits in positions `[39:4]`).
   - **Reconstruction:** Confirmed that `FIPA` page base is reconstructed by `(HPFAR_EL2 & 0x000000FFFFFFFFF0ULL) << 8`, combined with `FAR_EL2[11:0]`.

3. **Hypervisor Trap, Remap & Replay Flow:**
   - Validated that guest memory access encountering unmapped Stage-2 memory traps to EL2.
   - Verified that the simulated hypervisor recovers the exact faulting IPA frame from `HPFAR_EL2`, creates a valid 16KB descriptor in physical RAM via `Stage2TranslationEngine::map_page_16k`, and resumes guest execution via simulated ERET.
   - Replay access succeeds immediately without fault.

---

## 2. Test Execution Summary

The test executable was verified in both Release and Debug configurations:

```
==================================================================
Stage 203.2: EL2 Syndrome & HPFAR Architectural Audit Tests
==================================================================
[Stage 203.2 Test 1] ESR_EL2 Architectural Oracles...
  -> ESR_EL2 architectural oracles verified.
[Stage 203.2 Test 2] HPFAR_EL2 Field Layout & IPA Reconstruction...
  -> HPFAR_EL2 field layout & reconstruction verified.
[Stage 203.2 Test 3] Stage-2 Fault Remap & Replay Lifecycle...
  -> Stage-2 fault remap & replay lifecycle verified.
------------------------------------------------------------------
ALL STAGE 203.2 TESTS PASSED.
```

---

## 3. Acceptance Result

- Authoritative field-level tests for fault types, levels, and address boundaries: **PASS**
- Defect identified, resolved, and verified: **PASS**
- Zero compiler warnings (`/W4 /WX /permissive-`): **PASS**
- **Result: PASS**
