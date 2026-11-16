# WinIOS Campaign 202 — Stage 202.1 Evidence Correction Addendum

**Campaign:** 202  
**Stage:** 202.1  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Architecture:** Windows 11 x64  
**Date:** November 16, 2026  
**Status:** Canonical Addendum to Campaign 201 Evidence  

---

## 1. Executive Summary

In adherence to strict technical truth and reproducibility, this addendum audits and reconciles four specific evidentiary items from Campaign 201 closeout:
1. Artifact manifest count reconciliation (35 vs 36).
2. Standardization of differential vs specification-derived testing terminology.
3. Explicit boundary clarification regarding snapshot coverage and PAC keyring exclusion.
4. Adoption of evidence-bounded concurrency and race testing language.

---

## 2. Manifest Count Reconciliation

### 2.1 The Discrepancy
In Campaign 201 closeout documentation, the completion message reported 36 artifacts, whereas the text of Section 5 in `WINIOS_CAMPAIGN_201_FINAL_REPORT.md` stated:
`Total: 35, Verified: 35`.

### 2.2 Forensic Investigation & Root Cause
Inspection of `docs/campaign201/manifest.json` and `scratch/generate_campaign201_manifest.ps1` revealed:
- **Phase 1 (Drafting):** Initially, 17 markdown reports and 15 C++ unit test files were tracked, along with 2 scratch scripts (`generate_campaign201_manifest.ps1` and `verify_campaign201_manifest.ps1`), yielding 34 artifacts.
- **Phase 2 (Intermediate):** During text drafting of the final report, a placeholder of 35 artifacts was entered.
- **Phase 3 (Final Execution):** Two additional verification scripts created during Campaign 201 (`audit_ctest_accounting.ps1` and `verify_campaign200_manifest.ps1`) were formally incorporated into `scratch/generate_campaign201_manifest.ps1`.
- **Final Result:** The authoritative `manifest.json` on disk and committed in `834fa49` contains exactly **36 artifacts**:
  * **17 Documentation Artifacts:** `docs/campaign201/*.md`
  * **4 Scratch Scripts:** `scratch/audit_ctest_accounting.ps1`, `scratch/generate_campaign201_manifest.ps1`, `scratch/verify_campaign200_manifest.ps1`, `scratch/verify_campaign201_manifest.ps1`
  * **15 C++ Unit Test Files:** `tests/unit/stage201_*.cpp`

### 2.3 Authoritative Resolution
The authoritative artifact count for Campaign 201 is **36 artifacts**. Executing `powershell -ExecutionPolicy Bypass -File scratch/verify_campaign201_manifest.ps1` validates all 36 items with 100% SHA-256 integrity.

---

## 3. Standardization of Verification Terminology

Throughout Campaign 202 and subsequent work, the following precise taxonomy must be used without conflation:

| Classification Term | Definition & Qualification Criteria |
| :--- | :--- |
| **Specification-Derived** | Expected architectural behavior, flag calculations, or register masking derived mathematically from ARM DDI 0487 or Darwin ABI definitions. |
| **Independent-Vector-Tested** | Expected outputs originate from static, independently generated or published test vectors (e.g. NIST vectors, cryptographic standard vectors). |
| **Differentially Tested** | Guest execution results are compared step-for-step against a distinct, independently executing reference emulator (e.g. Unicorn 2.1.4). |
| **Integration-Tested** | Multiple emulated subsystems (e.g. MMU + CPU + GIC + Bus) are exercised in conjunction to verify mutual contracts. |
| **Approximated** | Behavior intentionally models a functional subset of the physical/OS specification for research utility. |
| **Unsupported** | Functionality is completely outside the emulation scope (e.g. Apple Secure Enclave hardware, physical PCIe link training). |

A mathematical calculation model must never be characterized as an "independently executing reference emulator".

---

## 4. Snapshot State Coverage & PAC Keyring Exclusions

### 4.1 Boundary Clarification
Campaign 201 established that `.wsnap` v2 snapshots capture the 1056-byte `SmpCoreState` (general-purpose registers X0–X30, PC, SP_ELx, CurrentEL, PSTATE, system registers SCTLR/TCR/TTBR/MAIR, timers, and debug state) along with guest RAM, scheduler state, and exclusive reservations.

### 4.2 PAC Keyring Security Invariant
- **PAC Keyring Exclusion:** The 5-key PAC keyring (APIA, APIB, APDA, APDB, APGA) resides in the CPU hardware instance and is **intentionally excluded** from the serialized `.wsnap` v2 architectural frame.
- **Restoration Invariant:** Upon snapshot restore into a fresh CPU instance, PAC keyrings are **not restored from disk**; rather, they reset to architectural power-on default keys (`0x847291a5e301bfcdULL`, `0x1928374655aa33ffULL`).
- **Documentation Policy:** The platform must not be described as providing "complete full-platform serialization" without acknowledging that hardware security root secrets (PAC keys) and translation caches (TLB) are excluded and reset by architectural policy.

---

## 5. Evidence-Bounded Concurrency Language

Stress tests (such as `stage201_16_smp_stress_tests`) validate high-contention atomic lock acquisition across 1 to 8 cores over thousands of iterations. However, stress testing cannot mathematically prove the absence of race conditions across every possible execution path.

**Mandatory Phrasing Rule:**
- **Approved:** "No data races, deadlocks, or lost updates were observed across 50,000 iterations under 8-core concurrency stress."
- **Forbidden:** "The platform is completely race-free."
