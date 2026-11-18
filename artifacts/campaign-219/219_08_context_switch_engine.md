# WinIOS Campaign 219 — Stage 219.8: Context Switch Engine & Register Preservation

## 1. Context Switch Engine Workflow

`MultiProcessScheduler::perform_context_switch` is the core dispatch primitive responsible for atomic CPU state transition between two threads.

The execution sequence is:

```
[ Step 1: Save Outgoing Thread State ]
    prev_thread->tcb().pc        = cpu.get_program_counter()
    prev_thread->tcb().sp_el0     = cpu.get_sp_el0()
    prev_thread->tcb().spsr_el1   = cpu.get_spsr_el1()
    prev_thread->tcb().tpidr_el0  = cpu.get_tpidr_el0()
    prev_thread->tcb().gpr[0..30] = cpu.read_register(0..30)

[ Step 2: Address Space Switching (Cross-Process Only) ]
    if next_thread->owner_pid() != current_pid:
        target_ttbr0 = uas_map_[next_pid]->root_table_pa()
        if target_ttbr0 != current_ttbr0:
            cpu.get_mmu().set_ttbr0_el1(target_ttbr0)
            cpu.get_mmu().invalidate_tlb_all()
            stats_.total_address_space_switches++

[ Step 3: Restore Incoming Thread State ]
    cpu.set_program_counter(next_thread->tcb().pc)
    cpu.set_sp_el0(next_thread->tcb().sp_el0)
    cpu.set_spsr_el1(next_thread->tcb().spsr_el1)
    cpu.set_tpidr_el0(next_thread->tcb().tpidr_el0)
    cpu.set_current_el(0) // Userspace EL0
    cpu.write_register(0..30, next_thread->tcb().gpr[0..30])
    
[ Step 4: Bookkeeping & Counter Update ]
    next_thread->set_state(ThreadState::Running)
    current_thread_ = next_thread
    quantum_remaining_ = 4
    stats_.total_context_switches++
```

## 2. Verification

Tested in `stage219_8_scheduler_execution_tests.exe`:
- Switching between Thread A (PID 100) and Thread B (PID 101).
- Hardware `TTBR0_EL1` switched from `root_a` to `root_b`.
- CPU general registers X0 through X30 and X5 modified during execution accurately preserved across switches.
- Total context switches and address space switches tracked accurately.
