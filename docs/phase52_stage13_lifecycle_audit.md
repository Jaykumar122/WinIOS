# WinIOS Phase 52 Stage 13: CPU Lifecycle and Context-Change Architecture Audit

## 1. Executive Summary & Objective

This audit inspects all CPU lifecycle transitions, execution states, exception level changes, stack pointer register-bank selections, and context management pathways present in the WinIOS codebase. The objective of Phase 52 Stage 13 is to instrument:
1. `TraceEventType::CPUStarted` (Type 10) — Accurate CPU initialization and activation.
2. `TraceEventType::CPUStopped` (Type 11) — Genuine CPU shutdown and stop transitions.
3. `TraceEventType::ContextChanged` (Type 12) — Architectural execution-context changes (Exception Level transitions, register-bank switching, exception returns).

All trace events must reuse the existing fixed 64-byte `TraceEvent` schema, `TraceManager`, SPSC ring buffer, and per-core monotonic sequence generator without introducing any secondary frameworks, OS process/thread scheduler abstractions, or altering binary formats.

---

## 2. Audit of Existing CPU Lifecycle

### 2.1 CPU Construction (`CPU::CPU()`, `src/cpu/cpu.cpp:3772`)
- **Implementation**:
  ```cpp
  CPU::CPU() {
      reset();
  }
  ```
- **Semantics**: C++ host object instantiation. All architectural registers, stack pointers, and flags are zeroed.
- **Trace Classification**: **NO EVENT**. The prompt explicitly mandates: *"Do not emit CPUStarted simply because a CPU object was constructed."*

### 2.2 CPU Reset (`CPU::reset()`, `src/cpu/cpu.cpp:3869`)
- **Implementation**:
  Clears general registers `registers_`, vector registers `v_registers_`, `sp_el0_`, `sp_el1_`, `sp_el2_`, `program_counter_`, `nzcv_`, `fpcr_`, `fpsr_`, `exclusive_monitor_`.
  Restores EL1h bare-metal boot defaults: `current_el_ = 1`, `sp_sel_ = true`, `daif_ = 0xFu`, `il_ = false`, `pstate_ss_ = false`.
  Resets PMU, VirtualTime per-core state, GIC, DebugArchitecture, and clears `is_halted_ = false`, `waiting_for_interrupt_ = false`, `waiting_for_event_ = false`, and `instruction_trace_sequence_ = 0`.
- **Semantics**: Clears and reinitializes state to power-on defaults.
- **Trace Classification**: **NO EVENT**. Reset prepares the core for configuration; execution has not started.

### 2.3 Guest Entry Point Initialization (`CPU::initialize_entry()`, `src/cpu/cpu.cpp:3943`)
- **Implementation**:
  Validates 4-byte entry address alignment and RAM limits, calls `reset()`, and sets `program_counter_ = entry_address`.
- **Semantics**: Configures the initial instruction entry point.
- **Trace Classification**: **NO EVENT**. CPU is configured, but not yet activated.

### 2.4 CPU Startup & Activation Boundary (`CPUStarted`, Type 10)
- **Startup Points in WinIOS**:
  1. **Explicit Startup (`CPU::start()`)**:
     Transitions the CPU from unstarted to active running state (`is_started_ = true`, `is_stopped_ = false`, `is_halted_ = false`).
  2. **Implicit First-Step Startup**:
     If `CPU::step()` or `CPU::run()` is called on an unstarted CPU (`!is_started_ && !is_stopped_ && !is_halted_`), the CPU automatically activates, emitting `CPUStarted` at sequence 1, strictly preceding the first `InstructionRetired` event at sequence 2.
  3. **SMP Core Initialization (`SMPCluster::initialize_core()`, `src/cpu/smp_cluster.cpp:62`)**:
     Primary/secondary core entry configuration and activation.
  4. **SMP Secondary Core Start (`SMPCluster::start_secondary_core()`, `src/cpu/smp_cluster.cpp:73`)**:
     Secondary core startup from reset with specific entry address.
  5. **PSCI CPU_ON (`PSCIService::do_cpu_on()`, `src/cpu/psci.cpp:184`)**:
     Target core transitions `Off -> OnPending -> On`, invoking `start_secondary_core`.
- **Idempotency**: Once `is_started_ == true`, repeated calls to `start()` or subsequent `step()` calls do **not** generate duplicate `CPUStarted` events.
- **Resume vs Startup**: Resuming a temporarily halted core via `resume_core()` or waking from WFI/WFE does **not** emit `CPUStarted` because the CPU was already started and has not been shut down.

### 2.5 CPU Stop & Shutdown Boundary (`CPUStopped`, Type 11)
- **Distinction of Stopped States**:
  - **Temporary Halt (`is_halted_ = true`)**:
    Invoked by `SMPCluster::halt_core()` during scheduling pauses. The core retains state and may resume. **NO `CPUStopped` EVENT**.
  - **Processor Sleep (WFI/WFE)**:
    Invoked by guest `WFI` or `WFE` instructions (`waiting_for_interrupt_ = true`, `waiting_for_event_ = true`). Processor waits for an interrupt. **NO `CPUStopped` EVENT**.
  - **Host Debugger Pause / Stepping**:
    Host debugger calling `Debugger::step()` or `continue_execution()` pausing execution. **NO `CPUStopped` EVENT**.
  - **Execution Limit Reached**:
    Host runner reaching `max_instructions` in `CPU::run()` or `GuestRuntime::execute()`. This is an artificial host execution limit, not an architectural guest shutdown. **NO `CPUStopped` EVENT**.
  - **Genuine CPU Shutdown / Power-Down**:
    1. **Explicit Stop (`CPU::stop(stop_reason)`)**:
       Explicit lifecycle termination: `is_stopped_ = true`, `is_started_ = false`, `is_halted_ = true`. Emits exactly one `CPUStopped` event.
    2. **PSCI CPU_OFF (`PSCIService::do_cpu_off()`, `src/cpu/psci.cpp:221`)**:
       Guest requests caller core shutdown. Core transitions to `CorePowerState::Off`, halting core. Emits `CPUStopped` with reason `PSCI_CPU_OFF`.
    3. **PSCI SYSTEM_OFF (`PSCIService::do_system_off()`, `src/cpu/psci.cpp:309`)**:
       Guest requests system power-off. All cores transition to `CorePowerState::Off` and halt. Emits `CPUStopped` with reason `PSCI_SYSTEM_OFF` on all active cores.
- **Idempotency**: Repeated calls to `stop()` when `is_stopped_ == true` are ignored and generate zero duplicate events.
- **CPU Destruction**: `CPU::~CPU()` destroys host resources safely without emitting late trace events.

---

## 3. Audit of Context Management Architecture

### 3.1 Exception-Level Transitions
WinIOS implements four architectural Exception Levels:
- **EL0**: Unprivileged user execution state (`current_el_ == 0`).
- **EL1**: Privileged OS supervisor kernel state (`current_el_ == 1`).
- **EL2**: Hypervisor virtualization state (`current_el_ == 2`, Phase 40).
- **EL3**: Secure Monitor firmware state (`current_el_ == 3`, Phase 47).

### 3.2 Exception Entry Transitions
Occurs across synchronous exceptions (`raise_synchronous_exception`, `raise_el2_exception`, `raise_el3_exception`) and asynchronous interrupts (`raise_asynchronous_exception`, `raise_el2_asynchronous_exception`, `raise_el3_asynchronous_exception`):
- Previous context: `current_el_`, `sp_sel_`, `daif_`, `pc`.
- New context: target EL (1, 2, or 3), `sp_sel_ = true` (ELxh), `daif_ = 0xF` (all masked), vector `program_counter_`.
- Architectural context transition:
  Whenever `prev_el != target_el` or `prev_spsel != new_spsel` or exception occurs:
  `ContextChanged` is emitted immediately after `ExceptionEntered`:
  - `prev_pc`: interrupted instruction PC
  - `new_pc`: vector destination address (`VBAR_ELx + offset`)
  - `prev_el`: source EL
  - `new_el`: target EL
  - `reason`: `ContextChangeReason::ExceptionEntry` (1)

### 3.3 Exception Return Transitions (`ERET`, `src/cpu/cpu.cpp:6975`)
Restores architectural context from `SPSR_ELx` and `ELR_ELx`:
- Target EL: `(spsr & 0x1F) >> 2`.
- Target SP select: `spsr & 1`.
- Restored PC: `elr_elx_`.
- Architectural context transition:
  `ContextChanged` is emitted immediately after `ExceptionReturned`:
  - `prev_pc`: `ERET` instruction address
  - `new_pc`: restored return address (`ELR_ELx`)
  - `prev_el`: current EL before ERET (e.g. EL1 or EL2)
  - `new_el`: restored EL from SPSR (e.g. EL0 or EL1)
  - `reason`: `ContextChangeReason::ExceptionReturn` (2)

### 3.4 Register-Bank Selection Transitions (`MSR SPSel`)
Executed via:
1. `MSR SPSel, #imm1` (`src/cpu/cpu.cpp:7113`):
   Sets `sp_sel_ = (crm & 1) != 0`.
2. `MSR SPSel, Xt` (`src/cpu/cpu.cpp:8062`):
   Sets `sp_sel_ = (val & 1) != 0`.
- Architectural context transition:
  If `sp_sel_` changes (`old_spsel != new_spsel`), the CPU switches between `SP_EL0` and `SP_ELx`. This is an architectural register-bank selection context change:
  - `prev_pc`: `MSR` instruction address
  - `new_pc`: next instruction address (`instruction_address + 4`)
  - `prev_el`: `current_el_`
  - `new_el`: `current_el_`
  - `prev_spsel`: `old_spsel`
  - `new_spsel`: `new_spsel`
  - `reason`: `ContextChangeReason::RegisterBankSelect` (3)
- If `old_spsel == new_spsel`, zero `ContextChanged` events are emitted.

### 3.5 Non-Context Modifying Instructions
The following operations do **NOT** modify architectural context and must emit **ZERO** `ContextChanged` events:
- General-purpose register operations (`ADD`, `SUB`, `MOV`, `LDR`, `STR`, `STP`, `LDP`).
- Condition flag updates (`CMP`, `ADDS`).
- Unconditional or conditional branches (`B`, `B.cond`, `BL`, `BR`, `BLR`).
- System register reads/writes that do not alter EL or stack pointer selection (e.g., `MSR DAIFSet`, `MSR TPIDR_EL0`).

---

## 4. TraceEvent 64-Byte Schema Encoding

### 4.1 `CPUStarted` (Type 10)
| Field | Value | Description |
| :--- | :--- | :--- |
| `sequence_number` | `++instruction_trace_sequence_` | Monotonic CPU sequence number |
| `virtual_time` | `virtual_time_->get_physical_counter()` | Timestamp |
| `pc` | `program_counter_` | Initial entry PC |
| `address` | `program_counter_` | Entry address |
| `data` | `0` | Reserved / initial payload |
| `opcode_or_syndrome`| `0` | Reserved |
| `flags` | `(current_el_ > 0 ? IsPrivileged : None)` | Flags |
| `event_type` | `TraceEventType::CPUStarted` (10) | Type 10 |
| `core_id` | `core_id_` | CPU core ID |
| `exception_level` | `current_el_` | Initial EL (typically 1) |
| `address_space` | `TraceAddressSpace::Virtual` | Virtual address space |
| `access_size` | `0` | Zero |
| `reserved` | `0` | Strict zero |

### 4.2 `CPUStopped` (Type 11)
| Field | Value | Description |
| :--- | :--- | :--- |
| `sequence_number` | `++instruction_trace_sequence_` | Monotonic CPU sequence number |
| `virtual_time` | `virtual_time_->get_physical_counter()` | Timestamp |
| `pc` | `program_counter_` | Shutdown PC |
| `address` | `0` | Zero |
| `data` | `stop_reason` | Stop reason (0=Explicit, 1=PSCI CPU_OFF, 2=PSCI SYSTEM_OFF) |
| `opcode_or_syndrome`| `0` | Reserved |
| `flags` | `(current_el_ > 0 ? IsPrivileged : None)` | Flags |
| `event_type` | `TraceEventType::CPUStopped` (11) | Type 11 |
| `core_id` | `core_id_` | CPU core ID |
| `exception_level` | `current_el_` | Final EL |
| `address_space` | `TraceAddressSpace::Virtual` | Virtual address space |
| `access_size` | `0` | Zero |
| `reserved` | `0` | Strict zero |

### 4.3 `ContextChanged` (Type 12)
| Field | Value | Description |
| :--- | :--- | :--- |
| `sequence_number` | `++instruction_trace_sequence_` | Monotonic CPU sequence number |
| `virtual_time` | `virtual_time_->get_physical_counter()` | Timestamp |
| `pc` | `prev_pc` | Source PC (e.g. faulting PC or ERET address) |
| `address` | `new_pc` | Target PC (e.g. vector address or restored return address) |
| `data` | `prev_el \| (new_el << 2) \| (prev_spsel << 4) \| (new_spsel << 5)` | Packed context metadata |
| `opcode_or_syndrome`| `static_cast<uint32_t>(reason)` | 1=ExceptionEntry, 2=ExceptionReturn, 3=RegisterBankSelect |
| `flags` | `HasVirtualAddress \| HasData \| (new_el > 0 ? IsPrivileged : None)` | Context flags |
| `event_type` | `TraceEventType::ContextChanged` (12) | Type 12 |
| `core_id` | `core_id_` | CPU core ID |
| `exception_level` | `new_el` | Destination Exception Level |
| `address_space` | `TraceAddressSpace::Virtual` | Virtual address space |
| `access_size` | `0` | Zero |
| `reserved` | `0` | Strict zero |

---

## 5. Event Ordering & Causal Pairing Rules

1. **CPU Startup**:
   - `CPUStarted` (seq 1, `pc = entry_pc`)
   - `InstructionRetired` (seq 2, first instruction)
2. **CPU Shutdown**:
   - `InstructionRetired` (seq $N-1$, final instruction before shutdown)
   - `CPUStopped` (seq $N$, `pc = shutdown_pc`)
3. **Synchronous Exception Entry (e.g. SVC from EL0 to EL1)**:
   - `InstructionRetired` (seq $N-1$, `SVC #0` instruction)
   - `ExceptionEntered` (seq $N$, `target_el = 1`, `vector = VBAR + 0x400`)
   - `ContextChanged` (seq $N+1$, `prev_el = 0`, `new_el = 1`, `reason = ExceptionEntry`)
4. **Asynchronous Interrupt Delivery (e.g. Timer IRQ)**:
   - `InstructionRetired` (seq $N-2$, previous instruction)
   - `InterruptDelivered` (seq $N-1$, `intid = 30`)
   - `ExceptionEntered` (seq $N$, `vector = VBAR + 0x280`)
   - `ContextChanged` (seq $N+1$, `reason = ExceptionEntry`)
5. **Exception Return (ERET from EL1 to EL0)**:
   - `InstructionRetired` (seq $K-2$, `ERET` instruction)
   - `ExceptionReturned` (seq $K-1$, `target_el = 0`, `target_pc = ELR`)
   - `ContextChanged` (seq $K$, `prev_el = 1`, `new_el = 0`, `reason = ExceptionReturn`)
6. **Register Bank Selection (MSR SPSel, #0 in EL1)**:
   - `InstructionRetired` (seq $M-1$, `MSR SPSel` instruction)
   - `ContextChanged` (seq $M$, `prev_el = 1`, `new_el = 1`, `prev_spsel = 1`, `new_spsel = 0`, `reason = RegisterBankSelect`)

---

## 6. SMP & SPSC Ring Buffer Concurrency

- Each CPU core maintains its own `instruction_trace_sequence_` counter.
- In SMP configurations, each core submits strictly to its designated lock-free SPSC trace ring buffer (`trace_manager_->submit(ev)` routes per core ID).
- Producer ownership is strictly thread-confined: host execution threads and parallel worker threads only submit to their own core's ring.
- No global locks are added to trace paths; the SPSC single-producer / single-consumer guarantee is strictly preserved.
