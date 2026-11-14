# WinIOS Campaign 197: Stage 197.14 Report
## Memory Ordering Under Virtual Memory, TLB Invalidation & Page Reuse (Acceptance Test K)

**Target Device:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Architecture:** Windows 11 x64, Visual Studio 18 2026, MSVC C++20  
**Evidence Classification:** `GUEST_MEMORY_MODEL` / `GUEST_EL1` / `SYNTHETIC_KERNEL`  
**Test Suite:** `tests/unit/stage197_14_tlbi_page_reuse_tests.cpp`  

---

### 1. Executive Summary

In an ARMv8-A SMP virtual memory architecture, modifying translation table descriptors in memory is not automatically coherent with other processor cores' TLBs (Translation Lookaside Buffers). ARM DDI 0487 Section B2.3, B2.4, and D5.7 define strict architectural rules for updating page tables, broadcasting TLB invalidations (`TLBI`), and synchronizing processor pipelines.

This stage verifies that:
1. **Cross-Core TLBI Broadcasts (`TLBI *IS`):** Any core unmapping or altering a virtual-to-physical mapping broadcasts an invalidation across the SMP cluster (`broadcast_tlbi_va`, `broadcast_tlbi_asid`, `broadcast_tlbi_all`).
2. **Page Unmapping Safety:** When Core 0 unmaps a page and executes the barrier-TLBI sequence, Core 1's cached TLB entry is eliminated immediately. Subsequent memory accesses by Core 1 generate a synchronous Stage-1 Translation Fault (`MmuFaultType::Translation`, DFSC=0x07) rather than returning stale cached physical memory.
3. **Page Remapping and Reuse:** When a page is remapped from physical address $PA_A$ to $PA_B$, broadcasting TLBI invalidates the old cached translation on Core 1 so subsequent reads resolve to $PA_B$ and reflect updated contents.
4. **ASID Isolation:** TLBI by ASID (`TLBI ASIDE1IS`) purges only non-global entries matching the specified ASID, leaving other ASID translations intact.
5. **Architectural Barrier Sequence:** The full ARMv8-A maintenance sequence:
   $$\text{Store Descriptor} \longrightarrow \text{DSB ISHST} \longrightarrow \text{TLBI VAE1IS} \longrightarrow \text{DSB ISH} \longrightarrow \text{ISB}$$
   is strictly enforced and validated.

---

### 2. Architectural Maintenance Protocol

```
  Core 0 (Page Table Modifier)                     Core 1 (Observer / Worker)
  ----------------------------                     --------------------------
  1. Write Descriptor (PTE) in RAM
  2. DSB ISHST (ensure PTE visible to walk)
  3. TLBI VAE1IS / ASIDE1IS (broadcast) ----------> Inter-core TLB Invalidation
  4. DSB ISH (wait for TLBI completion)             (Core 1 drops cached entry)
                                                    ISB (Context Synchronization)
                                                    Core 1 accesses VA:
                                                      -> TLB Miss
                                                      -> Page table walk
                                                      -> Resolves updated PTE / Faults!
```

---

### 3. Empirical Test Results

| Test Scenario | Condition Tested | Expected Outcome | Actual Result | Status |
|:---|:---|:---|:---|:---:|
| `test_cross_core_page_unmapping_and_fault` | Core 0 clears PTE; broadcasts `TLBI VAE1IS` | Core 1 drops cached translation; raises Translation Fault | DFSC=0x07 raised; 0 bytes leaked | **PASS** |
| `test_cross_core_page_remapping_and_reuse` | Core 0 updates VA from $PA_{old}$ to $PA_{new}$; broadcasts TLBI | Core 1 accesses VA; reads updated $PA_{new}$ data | Reads `0x9999888877776666ULL` | **PASS** |
| `test_asid_isolated_invalidation` | Core 1 caches ASID 1 and ASID 2; Core 0 broadcasts `TLBI ASIDE1IS (ASID=1)` | ASID 1 purged; ASID 2 preserved | TLB entries: 2 -> 1 (ASID 2 retained) | **PASS** |

---

### 4. Conformance Statement

WinIOS conforms to ARMv8-A Section D5.7 and B2.3 rules regarding TLB invalidation and memory ordering under virtual address translation. Cross-core TLBI broadcasts safely prevent stale reads, data leakage, and translation desynchronization across multi-core guest environments.
