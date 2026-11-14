# Stage 195.15 — Demand-Paging Concurrency & Memory Reclamation (Mandatory Test H)

## Subsystem Overview
- **Component**: Virtual Memory Manager, Demand Paging Handler, Physical Page Manager & TLB Invalidation Coherence
- **Architectural Classification**: `GUEST_EL1` / `SYNTHETIC_KERNEL`
- **Device Research Target**: iPhone 15 (`iPhone15,4` / `D37AP` / `T8120`), ARM64 16KB Granule

---

## 1. Objectives & Mandatory Acceptance Criteria
Stage 195.15 addresses **Mandatory Test H: Concurrent memory faults and demand-paging race prevention**:
1. **Dual-Core Simultaneous Page Faults on Disjoint Pages**: Verify that two CPU cores (Core 0 and Core 1) encountering independent unmapped addresses (`0x10000000` and `0x10004000`) simultaneously trap into EL1 exception vectors (`VBAR_EL1 + 0x200`), allocate separate 16KB backing pages from the physical pool, and resume transparently with complete data isolation.
2. **Same-Page Concurrent Access (Atomic Resolution)**: Ensure that multiple cores touching the exact same unmapped virtual page resolve atomically without redundant allocation, preserving backing pool capacity and maintaining single-descriptor identity.
3. **Cross-Core TLB Invalidation Broadcast**: Verify that page remapping and permission escalation (e.g. read-only to read-write) broadcast TLB invalidations across cores (`TLBI VAAE1IS` / `TLBI VMALLE1IS`), guaranteeing TLB coherence and preventing stale translation faults.
4. **Mapping Removal & Physical Reclamation**: Validate that explicit unmapping (`VirtualPageTable::unmap_page`) and physical deallocation (`PhysicalPageManager::free_page`) safely restore page availability and correctly reinstate page faults upon subsequent memory accesses.
5. **Out-of-Memory (OOM) Safety**: Verify that when the physical backing pool is exhausted, the demand paging subsystem reports memory translation faults gracefully without corrupting page tables or physical memory.

---

## 2. Test Execution & Results
Executable: `stage195_15_demand_paging_concurrency_tests.exe`

| Test Name | Conditions | Observations | Status |
|:---|:---|:---|:---|
| `test_dual_core_disjoint_page_faults` | Core 0 touches `0x10000000`, Core 1 touches `0x10004000` | Both trap to EL1, allocate disjoint 16KB frames (`0x200000`, `0x204000`), ERET returns, load instructions re-execute cleanly | **PASSED** |
| `test_same_page_concurrent_resolution` | Cores 0 and 1 access shared VA `0x10008000` | Core 0 resolves fault in L3 table; Core 1 observes valid mapping without second fault; 1 backing frame allocated total | **PASSED** |
| `test_concurrent_mapping_and_tlb_invalidation` | Core 0 updates RO mapping to RW and new PA | `TLBI VAAE1IS` broadcast invalidates Core 1's TLB; Core 1 translation immediately updates to new PA and allows writes | **PASSED** |
| `test_mapping_removal_and_reclamation` | 4 pages mapped, Page 2 unmapped and freed | `PPM::free_page` restores free counter; MMU translation of unmapped VA reports translation fault; other 3 pages intact | **PASSED** |
| `test_out_of_memory_demand_fault_handling` | Invalid / exhausted backing address slot | Handler attempts resolution, memory translation remains unmapped; CPU re-traps without crashing or corrupting memory | **PASSED** |

---

## 3. Compliance and Sign-Off
- Zero warnings under MSVC `/W4 /WX /permissive-`.
- 100% of demand paging concurrency and reclamation tests passed.
- Mandatory Acceptance Test H fully satisfied.
