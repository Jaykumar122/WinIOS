# WinIOS Campaign 199: Stage-2 Translation Architecture Report

**Target Research Architecture:** ARMv8.5-A / ARM64e (iPhone 15 / Apple A16 Bionic `T8120`)  
**Granule Profile:** 16KB Translation Granule (`TG0 = 0b10`), 48-bit IPA (`T0SZ = 16`)  
**Classification:** `STAGE2_TRANSLATION`  
**Status:** FULLY IMPLEMENTED & VERIFIED  

---

## 1. Subsystem Overview

Campaign 199 established the foundational Stage-2 Virtual Memory System Architecture (VMSA) for WinIOS, enabling nested virtualization, guest OS isolation, and hardware-accurate memory translation. The implementation closely reflects the ARMv8-A architecture and Apple silicon design principles.

Key architectural properties implemented:
- **Configurable Translation Granule:** First-class support for 16KB granules (`TG0 = 0b10`), alongside 4KB (`0b00`) and 64KB (`0b01`) standards.
- **System Registers:** Full support for `HCR_EL2.VM` (virtualization enable), `VTCR_EL2` (Stage-2 translation control), and `VTTBR_EL2` (Virtual Translation Table Base Register with 16-bit VMID and base table pointer).
- **Hierarchical Table Walk:** Multi-level translation walk supporting Level 0 through Level 3 descriptors, block translations, and table descriptors.
- **Combined Translation Pipeline:** Integrated Stage-1 (VA -> IPA) and Stage-2 (IPA -> PA) walk within `winios::mmu::MMU`, ensuring that memory protections at both stages are strictly intersected.
- **Stage-2 TLB & Invalidation:** VMID-tagged Stage-2 TLB cache entries, accelerated lookup paths, and architectural broadcast TLB invalidation (`TLBI VMALLS12E1IS`, `TLBI IPAS2E1IS`).

---

## 2. Combined Translation Flow

In the combined translation model:
1. **Stage 1 Translation:**
   - Input: Virtual Address (VA).
   - If Stage 1 is enabled (via `SCTLR_EL1.M`), the MMU walks Stage-1 page tables (indexed by `TTBR0_EL1` or `TTBR1_EL1`).
   - If Stage 2 is also enabled, every table descriptor address in the Stage-1 walk is treated as an Intermediate Physical Address (IPA) and translated through Stage 2.
   - Result: Intermediate Physical Address (IPA).
2. **Stage 2 Translation:**
   - Input: Intermediate Physical Address (IPA).
   - The Stage-2 engine indexes the base table identified by `VTTBR_EL2.BADDR` for the active `VMID`.
   - Walks table levels based on `VTCR_EL2.SL0` and `T0SZ`.
   - Result: Output Physical Address (PA).
3. **Permission Intersection:**
   - Read permission: `S1_R && S2_R`
   - Write permission: `S1_W && S2_W`
   - Execute permission: `S1_X && S2_X`

---

## 3. Fault Handling & Syndrome Reporting

Stage-2 translation faults generate precise architectural exception syndromes delivered to EL2 (or synthetic hypervisor trap handler):
- **Translation Faults:** FSC `0x04 + level` (Level 0, 1, 2, or 3).
- **Access Flag Faults:** FSC `0x08 + level`.
- **Permission Faults:** FSC `0x0C + level`.
- Fault syndrome register `ESR_EL2` populated with:
  - `EC = 0x24` (Data Abort from lower EL) or `0x20` (Instruction Abort from lower EL).
  - `S1PTW` bit set if fault occurred during a Stage-1 table walk.
  - `WnR` indicates write vs read access.
  - Fault IPA recorded in `HPFAR_EL2`.

---

## 4. Verification Evidence

The Stage-2 translation subsystem was validated via dedicated unit test suites:
- `stage199_1_stage2_translation_tests`:
  - `test_disabled_stage2`: Pass-through validation when `HCR_EL2.VM == 0`.
  - `test_16k_granule_translation`: 16KB page walk, attribute extraction, and RW/NX permission checks.
  - `test_stage2_permission_faults`: RO page write fault generation and FSC validation.
  - `test_stage2_translation_faults`: Unmapped IPA fault generation.
- `stage199_2_combined_translation_tests`:
  - `test_combined_s1_s2_identity`: Pass-through translation validation.
  - `test_combined_s1_s2_translation`: VA -> IPA -> PA translation mapping verification.
  - `test_permission_intersection`: S1 RW + S2 RO enforcing Read-Only fault.
- `stage199_3_stage2_tlb_invalidation_tests`:
  - `test_stage2_tlb_vmid_tagging`: Isolation between VMID 1 and VMID 2 caches.
  - `test_tlbi_ipas2e1is`: Page-specific Stage-2 TLB invalidation.
  - `test_tlbi_vmalls12e1is`: Full VMID-scoped Stage-1/Stage-2 TLB flush.
