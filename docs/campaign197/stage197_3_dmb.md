# WinIOS Campaign 197 Stage 197.3: DMB Architectural Semantics

**Document ID:** `docs/campaign197/stage197_3_dmb.md`  
**Classification:** `GUEST_MEMORY_MODEL`  
**Acceptance Test:** Acceptance Test A (DMB semantics distinct from DSB and ISB)

---

## 1. Architectural Role of DMB

The Data Memory Barrier (`DMB`) ensures that all explicit memory accesses that appear in program order before the `DMB` instruction are observed by any observer within the specified shareability domain before any explicit memory access that appears in program order after the `DMB` instruction.

Crucially:
- DMB does **not** affect execution of instructions that do not access memory.
- DMB does **not** wait for memory accesses to reach endpoint completion (e.g. DRAM or device registers).
- DMB does **not** drain store buffers in weak-memory architectures, preserving out-of-order write latency hiding while guaranteeing observable sequencing.

---

## 2. Implementation Architecture

In `src/cpu/cpu.cpp` and `src/cpu/guest_memory_order.cpp`:
- `CPU::step()` inspects system instruction with `crn == 7u, op2 == 5u`.
- Enforces validity of `crm`: options {1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15} are accepted; all others trigger synchronous exception (`make_undefined_instruction`).
- Advances `dmb_count_` metric.
- Records barrier event in `GuestMemoryOrderModel::record_dmb()`, establishing observation fences between predecessor and successor events.
- Does **not** drain pending store buffer entries.

---

## 3. Test Verification (Acceptance Test A)

In `tests/unit/stage197_3_barrier_semantics_tests.cpp`:
1. All 12 valid CRM encodings decoded and executed successfully.
2. Reserved CRM values {0, 4, 8, 12} verified to trigger Undefined Instruction exceptions.
3. Pending store buffers verified to remain intact across DMB execution.
4. Independent pass verified in `stage197_3_barrier_semantics_tests.exe`.
