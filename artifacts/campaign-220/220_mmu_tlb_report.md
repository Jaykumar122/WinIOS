# WinIOS Campaign 220 — Stage 220.4 MMU Translation, TLB and ASID Correctness Report

## 1. Executive Summary

Stage 220.4 audited and verified the ARM64 translation engine, TLB tagging, and ASID management under multi-process conditions.
Key objectives achieved:
1. **16 KB Translation Granule & Root Alignment:** Enforced strict 16 KB page alignment for translation roots (`root_l2_table_pa % 0x4000 == 0`) and page tables.
2. **ASID Tagging & Context Switching:** Verified that `TTBR0_EL1` encodes the 16-bit ASID in bits [63:48]. Non-global user page translations (`nG = 1`) are tagged with the active ASID, preventing TLB hits across processes with identical virtual addresses.
3. **Selective TLB Invalidation:** Verified that `invalidate_tlb_asid(asid)` flushes entries belonging to a recycled or unmapped process without flushing entries of concurrent active processes.
4. **W^X & Permission Boundaries:** Validated that write attempts to read-only pages and execution attempts on writable pages generate genuine `MmuFaultType::Permission` faults with architectural FSC codes.
5. **EL0/EL1 Boundary Isolation:** Accesses to kernel-mapped pages from EL0 mode trigger permission faults.

---

## 2. Architectural Verification Matrix

| Component | Architecture Requirement | Implementation / Test Reference | Result |
| :--- | :--- | :--- | :---: |
| **Granule Alignment** | Root table and page addresses must be 16 KB aligned (`va & 0x3FFF == 0`) | `MMU::validate_table_alignment(..., GranuleType::Granule16KB)` | **VERIFIED** |
| **nG (Non-Global) Bit** | User pages must encode `desc \|= (1ULL << 11)` | `src/memory/virtual_page_table.cpp:53, 59, 65` | **VERIFIED** |
| **ASID in TTBR0** | `TTBR0_EL1[63:48]` holds 16-bit ASID | `UserAddressSpace::root_table_pa_with_asid()` | **VERIFIED** |
| **Cross-Process TLB Isolation** | Reading identical VA in ASID 11 vs ASID 22 returns distinct physical frames | `tests/unit/stage220_4_mmu_tlb_asid_tests.cpp:test_asid_tlb_isolation` | **VERIFIED** |
| **Targeted ASID Flush** | `invalidate_tlb_asid` purges only specified ASID entries | `tests/unit/stage220_4_mmu_tlb_asid_tests.cpp:test_asid_invalidation_and_reuse` | **VERIFIED** |
| **W^X Enforcement** | Executing from writable pages triggers permission fault | `tests/unit/stage220_4_mmu_tlb_asid_tests.cpp:test_wxn_and_permission_boundaries` | **VERIFIED** |
| **Translation Faults** | Access to unmapped VA triggers `MmuFaultType::Translation` | `src/cpu/mmu.cpp`, `stage220_4` | **VERIFIED** |
