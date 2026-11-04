# WinIOS Phase 52 Stage 13: CPU Lifecycle Feature Inventory

## 1. Overview

This document provides a comprehensive inventory of all CPU lifecycle operations, state transition mechanisms, power management interfaces, and execution controllers implemented in WinIOS. It establishes the exact operational status, tracing behavior, and trace event generation semantics for `TraceEventType::CPUStarted` and `TraceEventType::CPUStopped`.

---

## 2. CPU Lifecycle Feature Inventory

| Subsystem Component | Source Location | Operational Status | Tracing Classification | Generated Trace Event | Emitted Fields & Attributes |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **CPU Constructor** | `src/cpu/cpu.cpp:3772` | Implemented | Host memory initialization | **NONE** | No event emitted upon C++ object creation |
| **CPU Destructor** | `src/cpu/cpu.h:31` | Implemented | Host memory cleanup | **NONE** | No late event emitted during object teardown |
| **CPU Reset** | `src/cpu/cpu.cpp:3869` | Implemented | State restoration to EL1 defaults | **NONE** | No event emitted on reset |
| **CPU Entry Init** | `src/cpu/cpu.cpp:3943` | Implemented | Entry PC configuration | **NONE** | Configuration only; core not yet activated |
| **CPU Activation / Start** | `src/cpu/cpu.cpp` | Implemented | Core startup / first execution step | **CPUStarted (Type 10)** | `pc = entry_pc`, `address = entry_pc`, `core_id`, `el = current_el_` |
| **CPU Step Auto-Start** | `src/cpu/cpu.cpp:4711` | Implemented | First instruction step if unstarted | **CPUStarted (Type 10)** | Emitted once at sequence 1 prior to first retirement |
| **CPU Stop / Shutdown** | `src/cpu/cpu.cpp` | Implemented | Genuine CPU shutdown / termination | **CPUStopped (Type 11)** | `pc = shutdown_pc`, `core_id`, `el = current_el_`, `data = reason` |
| **Temporary Core Halt** | `src/cpu/smp_cluster.cpp:87` | Implemented | Scheduling pause / idle | **NONE** | Temporary halt does not emit `CPUStopped` |
| **Core Resume** | `src/cpu/smp_cluster.cpp:94` | Implemented | Awakening from temporary halt | **NONE** | Core was already started; does not emit `CPUStarted` |
| **WFI / WFE Sleep** | `src/cpu/cpu.cpp:7143` | Implemented | Architectural low-power wait | **NONE** | Architectural wait is not lifecycle shutdown |
| **WFI / WFE Wakeup** | `src/cpu/cpu.cpp:4176` | Implemented | Interrupt / SEV wakeup | **NONE** | Waking from sleep does not emit `CPUStarted` |
| **Debugger Step / Pause**| `src/debugger/debugger.cpp` | Implemented | Host debugging control | **NONE** | Debugger stop does not masquerade as shutdown |
| **Host Execution Limit** | `src/cpu/cpu.cpp:9160` | Implemented | `max_instructions` limit reached | **NONE** | Host limit is not guest CPU shutdown |
| **SMP Init Core** | `src/cpu/smp_cluster.cpp:62` | Implemented | Core 0 entry configuration & start | **CPUStarted (Type 10)** | Emits `CPUStarted` for core 0 |
| **SMP Start Secondary** | `src/cpu/smp_cluster.cpp:73` | Implemented | Secondary core startup | **CPUStarted (Type 10)** | Emits `CPUStarted` for secondary core ID |
| **PSCI CPU_ON** | `src/cpu/psci.cpp:184` | Implemented | Secondary core power-on via SMC | **CPUStarted (Type 10)** | Emits `CPUStarted` on target secondary core |
| **PSCI CPU_OFF** | `src/cpu/psci.cpp:221` | Implemented | Core power-off via SMC | **CPUStopped (Type 11)** | Emits `CPUStopped` with reason `PSCI_CPU_OFF` (1) |
| **PSCI SYSTEM_OFF** | `src/cpu/psci.cpp:309` | Implemented | Virtual system power-off via SMC | **CPUStopped (Type 11)** | Emits `CPUStopped` with reason `PSCI_SYSTEM_OFF` (2) |
| **PSCI SYSTEM_RESET** | `src/cpu/psci.cpp:318` | Implemented | Virtual system reset via SMC | **CPUStopped (Type 11)** | Emits `CPUStopped` with reason `PSCI_SYSTEM_RESET` (3) |

---

## 3. Idempotency and Duplication Safeguards

1. **`is_started_` Guard**:
   - Flag set upon first activation.
   - Subsequent calls to `start()` or `step()` verify `!is_started_` before emitting `CPUStarted`.
   - Guaranteed exactly one `CPUStarted` event per qualifying startup.
2. **`is_stopped_` Guard**:
   - Flag set upon genuine shutdown.
   - Subsequent calls to `stop()` verify `!is_stopped_` before emitting `CPUStopped`.
   - Guaranteed exactly one `CPUStopped` event per qualifying shutdown.
3. **Reset Semantics**:
   - `CPU::reset()` resets `is_started_ = false; is_stopped_ = false;`.
   - Allows a core to be re-activated and emit a single `CPUStarted` upon its next lifecycle startup.
