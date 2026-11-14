# WinIOS Campaign 197 Stage 197.5: ISB Instruction Synchronization Semantics

**Document ID:** `docs/campaign197/stage197_5_isb.md`  
**Classification:** `GUEST_MEMORY_MODEL`  
**Acceptance Test:** Acceptance Test C (ISB instruction synchronization independently verified)

---

## 1. Architectural Role of ISB

The Instruction Synchronization Barrier (`ISB`) flushes the pipeline in the processor, so that all instructions that come after an `ISB` instruction in program order are fetched from cache or memory after the `ISB` instruction has completed.

Key architectural boundaries:
- **ISB is NOT a data barrier:** It does not order or complete data memory reads and writes.
- **Context synchronization:** Ensures effects of context-altering operations (such as system register writes: SCTLR, TCR, TTBR, CPACR, and page table modifications) are visible to instructions fetched after the ISB.
- **CRM Constraint:** According to ARM DDI 0487 Section C6.2.117, only `CRM = 0b1111` (`SY`) is standard and valid. All other CRM values (0 through 14) are unallocated/undefined.

---

## 2. Implementation Architecture

In `src/cpu/cpu.cpp` and `src/cpu/guest_memory_order.cpp`:
- `CPU::step()` inspects system instruction with `crn == 7u, op2 == 6u`.
- Enforces `crm == 15u`; all other CRM values raise `make_undefined_instruction`.
- Advances `isb_count_` metric.
- Flushes MMU TLB entries using `mmu_.invalidate_tlb_all()`.
- Records pipeline flush in `GuestMemoryOrderModel::record_isb()`.
- Does **not** issue a data memory fence and does **not** drain data store buffers.

---

## 3. Test Verification (Acceptance Test C)

In `tests/unit/stage197_3_barrier_semantics_tests.cpp`:
1. Valid `ISB SY` (CRM=15) verified to execute and increment `isb_count_`.
2. TLB invalidation verified upon ISB execution.
3. Data store buffer verified to remain intact (not drained) across ISB.
4. All invalid CRM values (0 through 14) verified to trigger Undefined Instruction exceptions.
5. Independent pass verified in `stage197_3_barrier_semantics_tests.exe`.
