# WinIOS — Phase 52, Stage 25: Event Source Coverage Matrix

## 1. Matrix Overview
This matrix maps each architectural event in the SNDL v2 stable registry to its concrete source implementation in the WinIOS codebase, verifying capture status, boundary type, payload requirements, and test coverage.

| Opcode | Event Name | Source Location | Boundary Type | Payload Requirements | Test Suite |
|---|---|---|---|---|---|
| `10` | `CoreCreated` | `SmpScheduler::register_core` | `CoreLifecycleBoundary` | `payload1 = core_id` | Group A, E |
| `11` | `CoreStarted` | `PSCI / SmpScheduler` | `CoreLifecycleBoundary` | `pc = entry_pc` | Group A, D |
| `12` | `CoreHalted` | `CPU::halt` | `CoreLifecycleBoundary` | `pc = halt_pc` | Group A, D |
| `13` | `CoreEnteredWfi`| `CPU::execute_wfi` | `CoreLifecycleBoundary` | `pc = wfi_pc` | Group A, D |
| `14` | `CoreEnteredWfe`| `CPU::execute_wfe` | `CoreLifecycleBoundary` | `pc = wfe_pc` | Group A, D |
| `15` | `CoreResumed` | `SmpInterruptRouter` | `CoreLifecycleBoundary` | `payload1 = intid` | Group A, D |
| `16` | `CoreStopped` | `PSCI CPU_OFF` | `CoreLifecycleBoundary` | `pc = stop_pc` | Group A, D |
| `20` | `QuantumStart` | `SmpScheduler::step_quantum` | `SchedulerBoundary` | `payload1 = quantum_budget` | Group B, C, G |
| `21` | `QuantumEnd` | `SmpScheduler::step_quantum` | `SchedulerBoundary` | `payload1 = executed, payload2 = reason` | Group B, G |
| `22` | `EarlyTruncation`| `SmpScheduler::step_quantum` | `SchedulerBoundary` | `payload1 = reason` | Group B, G |
| `23` | `CoreSelected` | `SmpScheduler::step_quantum` | `SchedulerBoundary` | `payload1 = reason` | Group B, G |
| `24` | `BarrierEntry` | `SmpScheduler::sync_barrier` | `BarrierBoundary` | `payload1 = barrier_id` | Group F |
| `25` | `BarrierRelease`| `SmpScheduler::sync_barrier` | `BarrierBoundary` | `payload1 = barrier_id` | Group F |
| `30` | `ExternalInterruptAsserted` | `SmpInterruptRouter::inject_interrupt` | `AsynchronousInput` | `payload1 = intid, payload2 = target_core` | Group D |
| `31` | `InterruptRouted` | `SmpInterruptRouter::route_next` | `BetweenInstructions` | `payload1 = intid, payload2 = from_core` | Group D |
| `32` | `InterruptAccepted` | `GICv3 CPU Interface` | `ExceptionBoundary` | `payload1 = intid, payload2 = target_el` | Group D |
| `33` | `ExceptionTaken`| `SmpScheduler::step_quantum` | `ExceptionBoundary` | `payload1 = syndrome, payload2 = target_el` | Group B |
| `34` | `ExceptionReturned`| `CPU::execute_eret` | `ExceptionBoundary` | `payload1 = target_el, pc = return_pc` | Group D |
| `40` | `CntpctRead` | `CPU::read_cntpct` | `DuringInstruction` | `payload1 = counter_val` | Group E |
| `41` | `CntvctRead` | `CPU::read_cntvct` | `DuringInstruction` | `payload1 = counter_val` | Group E |
| `42` | `PmccntrRead`| `PMU::read_pmccntr` | `DuringInstruction` | `payload1 = pmu_val` | Group E |
| `43` | `TimerComparatorExpired` | `VirtualTimer::check_deadline` | `AsynchronousInput` | `payload1 = timer_id, payload2 = deadline` | Group E |
| `50` | `MmioReadResult`| `Memory::read_mmio` | `DuringInstruction` | `payload1 = addr, payload2 = val, payload3 = width` | Group C, E |
| `51` | `EntropyReadResult` | `CPU::execute_rndr` | `DuringInstruction` | `payload1 = entropy_val` | Group E |
| `52` | `HostInputInjected` | `SndlV2Recorder::record_host_input` | `AsynchronousInput` | `payload1 = input_data` | Group E |
| `60` | `MemoryBarrierSync` | `CPU::execute_dmb` | `BarrierBoundary` | `payload1 = barrier_type` | Group F |
| `61` | `ReservationSet` | `SmpExclusiveMonitor::load_exclusive` | `DuringInstruction` | `payload1 = granule_addr` | Group F |
| `62` | `ReservationCleared` | `SmpExclusiveMonitor::clear_reservation` | `DuringInstruction` | `payload1 = granule_addr` | Group F |
| `63` | `StoreExclusiveResult` | `SmpExclusiveMonitor::store_exclusive` | `DuringInstruction` | `payload1 = addr, payload2 = status (0/1)` | Group C, F |
