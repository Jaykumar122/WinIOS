# Software TLB Lifecycle & Invalidation Results
**WinIOS Software TLB Architecture & Invariant Validation**

---

## 1. TLB Structural Topology

- **Capacity:** 64 unified entries.
- **Replacement Policy:** Deterministic FIFO with circular index counter.
- **Tag Matching:**
  - Virtual Page Number (masked by `page_mask` for 4KB, 2MB, or 1GB pages).
  - ASID (Address Space Identifier) for non-global translations.
  - VMID (Virtual Machine Identifier) for Stage 2 translations.
  - Privilege and Access Kind (Read / Write / Execute).

---

## 2. Invalidation Operations Matrix

| Command | Architectural Invariant | Invalidation Scope | Verified in Test Suite |
|---|---|---|---|
| `tlb_invalidate_all()` | Flush all 64 entries | Sets `valid = false` for all entries | YES |
| `tlb_invalidate_va(va, asid)` | Flush matching VPN + ASID | Invalidates matching entries; preserves others | YES |
| `tlb_invalidate_asid(asid)` | Flush entire address space | Invalidates all entries matching ASID | YES |
| `tlb_invalidate_vmid(vmid)` | Flush guest VM translations | Invalidates all Stage 2 entries matching VMID | YES |

---

## 3. Hit/Miss Performance & Differential Invariance

In `mmu_conformance_tests` Test 4:
1. **Cold Miss:** Initial translation of VA `0x2050` walks the page table (4 memory reads), populating TLB entry with ASID `0x42`.
2. **Warm Hit:** Subsequent translation of VA `0x2080` resolves directly from TLB (0 memory reads), producing identical PA.
3. **ASID Invalidation:** `tlb_invalidate_asid(0x42)` invalidates entry.
4. **Post-Invalidation Miss:** Next translation re-executes table walk; producing identical PA.
5. Invariance: TLB caching produces **zero divergence** in physical addresses or architectural state compared to the non-cached reference model.
