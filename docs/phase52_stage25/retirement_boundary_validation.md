# WinIOS — Phase 52, Stage 25: Retirement Boundary Validation & Exception Invariants

## 1. Architectural Retirement Invariant
In AArch64, an instruction is defined as architecturally retired **only when its execution completes and all architectural side effects (register writeback, PC advancement, flag updates) are committed to state**.

If an instruction triggers a synchronous exception (e.g. unallocated opcode, page fault, alignment fault, SVC/HVC/SMC trap):
- Execution of the instruction is aborted.
- Hardware state branches to the exception vector.
- **The faulting instruction has NOT retired and must NOT increment instruction retirement counters**.

## 2. Implementation Hardening in Stage 25
In `src/cpu/smp_scheduler.cpp`:
```cpp
// 1. Execute instruction
slot.cpu->step(shared_memory_->raw_memory());

// 2. Check for synchronous exception BEFORE advancing counters
if (slot.cpu->exception_taken_in_step()) {
    reason = QuantumTruncationReason::ExceptionRaised;
    if (sndl_recorder_) {
        sndl_recorder_->record_exception_taken(selected_core, 0,
            static_cast<std::uint32_t>(slot.cpu->get_current_el()),
            slot.cpu->get_program_counter());
    }
    break; // Break without advancing retirement counters!
}

// 3. Only retired instructions advance global and per-core counters
executed++;
global_retirement_sequence_++;
if (sndl_recorder_) {
    sndl_recorder_->notify_instruction_retired(selected_core, 1);
}
```

## 3. Test Verification
Verified in `tests/unit/sndl_v2_hardening_tests.cpp` (Group B, Part 2):
- Faulting opcode `0x00000000` with `architectural_exceptions = true` triggers exception vector dispatch.
- `sched.get_slot(0).total_instructions_retired` is confirmed to be exactly 0.
- `ExceptionTaken` event is cleanly recorded with boundary `ExceptionBoundary`.
