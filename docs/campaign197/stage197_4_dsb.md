# WinIOS Campaign 197 Stage 197.4: DSB Completion Semantics

**Document ID:** `docs/campaign197/stage197_4_dsb.md`  
**Classification:** `GUEST_MEMORY_MODEL`  
**Acceptance Test:** Acceptance Test B (DSB completion behavior independently verified)

---

## 1. Architectural Role of DSB

The Data Synchronization Barrier (`DSB`) instruction completes when:
1. All explicit memory accesses of the specified type issued before the DSB have completed.
2. All cache and TLB maintenance operations issued before the DSB have completed.
3. No instruction that appears in program order after the DSB instruction can execute until the DSB has completed.

Unlike `DMB`, `DSB` forces hardware/simulated store buffers to completely drain to the memory subsystem before program execution continues.

---

## 2. Implementation Architecture

In `src/cpu/cpu.cpp` and `src/cpu/guest_memory_order.cpp`:
- `CPU::step()` inspects system instruction with `crn == 7u, op2 == 4u`.
- Enforces validity of `crm`: options {1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15} are accepted; all others trigger synchronous exception (`make_undefined_instruction`).
- Advances `dsb_count_` metric.
- Dispatches to `GuestMemoryOrderModel::record_dsb()`, which calls `drain_store_buffer(core_id)`.
- Applies host sequential consistency fence `std::atomic_thread_fence(std::memory_order_seq_cst)`.

---

## 3. Test Verification (Acceptance Test B)

In `tests/unit/stage197_3_barrier_semantics_tests.cpp`:
1. All 12 valid CRM encodings decoded and executed successfully.
2. Reserved CRM values {0, 4, 8, 12} verified to trigger Undefined Instruction exceptions.
3. Multiple pending store buffer entries queued prior to DSB verified to be completely drained upon DSB execution.
4. Independent pass verified in `stage197_3_barrier_semantics_tests.exe`.
