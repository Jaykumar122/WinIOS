# CAMPAIGN 193 — STAGE 193.2: EXCEPTION ENTRY ARCHITECTURE

- **Campaign:** 193
- **Stage:** 193.2
- **Status:** **COMPLETED — VERIFIED**
- **Test Target:** `stage193_2_exception_entry_tests` (5/5 tests passed in Release & Debug)

---

## 1. OBJECTIVE & IMPLEMENTATION

Stage 193.2 audited and validated the centralized guest exception-entry mechanism across all supported exception classes:
1. **Centralized Entry Dispatch (`deliver_architectural_exception`):**
   - Single common architectural entry point located in `src/cpu/cpu.cpp` lines 4380-4459.
   - Clears CPU and SMP cluster exclusive reservations.
   - Preserves previous processor state in `SPSR_EL1`:
     - NZCV condition flags.
     - DAIF interrupt mask bits.
     - Originating execution level (M[3:2] = 0 for EL0, 1 for EL1).
     - Originating stack pointer selection (M[0] = SPSel).
     - PSTATE.IL and PSTATE.SS.
   - Preserves return address in `ELR_EL1`.
   - Preserves exception syndrome in `ESR_EL1`.
   - Preserves fault address in `FAR_EL1` for address-related faults (data aborts, instruction aborts).
   - Updates execution state: `current_el_ = 1`, `sp_sel_ = true` (EL1h), `daif_ = 0xF` (all interrupts masked).
   - Computes architectural vector offset and branches to `VBAR_EL1 + vector_offset`.
2. **Exception Classes Verified:**
   - Synchronous exceptions from EL0: `SVC` (EC `0x15`, ISS captures imm16, return address = PC + 4).
   - Data Abort from EL0: EC `0x24`, WnR, DFSC, FAR_EL1 holds fault address.
   - Instruction Abort from EL0: EC `0x20`, IFSC, FAR_EL1 and ELR_EL1 hold fault address.
   - Asynchronous IRQ from EL0: vector offset `+0x480`, ELR_EL1 captures interrupted PC.
   - Asynchronous FIQ from EL0: vector offset `+0x500`, ELR_EL1 captures interrupted PC.
   - Synchronous exceptions from EL1 with SPx: vector offset `+0x200`, SPSR_EL1 captures EL1h (M=0x05).

---

## 2. TEST EVIDENCE

Target: `stage193_2_exception_entry_tests`
- `test_svc_entry_from_el0`: **PASS** (EC 0x15, vector offset +0x400, SPSR M=0x00, DAIF 0xF).
- `test_data_abort_entry_from_el0`: **PASS** (EC 0x24, FAR_EL1 captured, WnR=1, DFSC PermissionLevel3).
- `test_instruction_abort_entry_from_el0`: **PASS** (EC 0x20, FAR_EL1 captured, IFSC TranslationLevel2).
- `test_async_interrupt_entry_from_el0`: **PASS** (IRQ -> +0x480, FIQ -> +0x500, SPSR preserved).
- `test_sync_entry_from_el1h`: **PASS** (Sync from EL1h -> +0x200, SPSR M=0x05).

Both Release and Debug builds clean and 5/5 tests passed.
