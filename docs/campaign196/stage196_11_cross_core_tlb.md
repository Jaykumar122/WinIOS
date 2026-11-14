# Stage 196.11 — Cross-Core TLB & Memory Reclamation (Acceptance Test J)

## 1. Architectural Implementation
- **Break-Before-Make & TLBI Broadcast:**
  - In ARMv8-A multi-core architectures, page table modifications require broadcasting TLB invalidations to prevent stale translations before physical page reclamation or reuse.
  - `SMPCluster` implements broadcast primitives:
    - `broadcast_tlbi_va(sender, va, asid, last_level)`: Invalidate single virtual address across all peer cores.
    - `broadcast_tlbi_vaa(sender, va, last_level)`: Invalidate all ASIDs for VA.
    - `broadcast_tlbi_asid(sender, asid)`: Invalidate all translations for specific ASID.
    - `broadcast_tlbi_all(sender)`: Total cluster TLB purge.

## 2. Test Evidence (Acceptance Test J)
Verified via `stage196_11_tlb_and_context_tests`:
- Test J1: Core 1 populates TLB; Core 0 unmaps and broadcasts TLBI VA; Core 1 TLB entry is purged.
- Test J2: TLBI ASID cross-core invalidation.
- Test J3: TLBI ALL cross-core invalidation.

## 3. Evidence Classification
`GUEST_SMP`, `SYNTHETIC_KERNEL`. Tested in Debug and Release.
