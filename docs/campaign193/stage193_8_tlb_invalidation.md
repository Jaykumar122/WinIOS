# Stage 193.8 — TLB and Translation Invalidation

## 1. Objectives & Scope
Stage 193.8 validates the software TLB caching and architectural invalidation operations:
- Caching behavior and stale translation detection: verifying that modifying a page table entry in physical memory without issuing a TLBI operation results in a TLB hit returning the stale physical address, and that issuing `TLBI VAE1` invalidates the cached entry and causes subsequent accesses to walk the page table and retrieve the updated physical address.
- Address-specific invalidation (`TLBI VAE1`): invalidating a specific VA while retaining unrelated TLB entries (proving invalidation is not treated as an indiscriminate full flush).
- ASID-aware invalidation (`TLBI ASIDE1`): invalidating non-global entries belonging to a specified ASID while preserving entries belonging to other ASIDs.
- Global versus non-global mapping distinctions (`nG` bit): ensuring `TLBI ASIDE1` does not invalidate global (`nG = 0`) entries, while `TLBI VAAE1` (VA All ASIDs) invalidates both global and non-global entries matching the virtual address.
- Guest instruction execution of TLBI operations (`TLBI VMALLE1`, `TLBI VAE1`, `TLBI ASIDE1`) at EL1.
- EL0 privilege enforcement: executing `TLBI` at EL0 traps to EL1 vector `VBAR_EL1 + 0x400` with Exception Class `0x00` (`Unknown` / Undefined Instruction).

## 2. Implementation & Test Architecture
The test suite `tests/unit/stage193_8_tlb_invalidation_tests.cpp` exercises:
1. `test_tlb_caching_and_stale_translation_behavior`:
   - Configures 16KB granule, 36-bit VA (`TCR_EL1 = (2 << 14) | 28`).
   - Translates VA `0x1000` to PA `0x50000`, populating the TLB (1 entry).
   - Mutates physical L3 table descriptor to point to PA `0x70000`.
   - Accesses `0x1000` again: hits TLB, returns stale PA `0x50000`.
   - Executes `invalidate_tlb_va(0x1000, 1, false)`.
   - Accesses `0x1000`: misses TLB, walks page table, returns updated PA `0x70000`.
2. `test_address_specific_invalidation`:
   - Populates TLB with Page 0 (`0x1000`) and Page 1 (`0x5000`).
   - Executes `invalidate_tlb_va(0x1000, 1, false)`.
   - Verifies TLB entry count drops to 1, and the remaining entry is Page 1.
3. `test_asid_aware_invalidation`:
   - Populates TLB with non-global Page 0 for ASID 1 and non-global Page 1 for ASID 2.
   - Executes `invalidate_tlb_asid(1)`.
   - Verifies TLB entry count drops to 1, retaining the ASID 2 entry.
4. `test_global_vs_nonglobal_invalidation`:
   - Populates TLB with global entry (`nG = 0`) at `0x1000` and non-global entry (`nG = 1`) at `0x5000`.
   - Executes `invalidate_tlb_asid(1)`: global entry is preserved; non-global entry is evicted.
   - Executes `invalidate_tlb_vaa(0x1000, false)`: global entry is evicted.
5. `test_guest_execution_of_tlbi_and_el0_privilege_enforcement`:
   - Executes guest instruction `TLBI VMALLE1` (`0xD508871F`) at EL1: PC advances by 4, TLB is cleared.
   - Executes `TLBI VMALLE1` at EL0: traps to EL1 vector `VBAR_EL1 + 0x400`, EC `0x00`, `ELR_EL1` points to trapped instruction.

## 3. Evidence & Verification Results
- `tests/unit/stage193_8_tlb_invalidation_tests.cpp` (5/5 tests passed in both Debug and Release).
- CTest target: `stage193_8_tlb_invalidation_tests`.
- Classification: `HOST_MODEL` / `GUEST_EL1` / `GUEST_EL0` / `GUEST_EXCEPTION`.
