# WinIOS Phase 52 Stage 11: Debug Subsystem and Debugger Architecture Audit

## 1. Executive Summary

This document performs the mandatory pre-implementation architecture audit of the WinIOS debugging subsystem for Phase 52 Stage 11 (`TraceEventType::DebugEvent`, Type 9).

The audit establishes:
- The exact operational boundaries of host-side debugging (`Debugger`) versus architectural guest debugging (`DebugArchitecture`).
- The execution lifecycle and precise detection points for hardware execution breakpoints, hardware data watchpoints, and hardware single-step.
- The status of software breakpoints (`BRK`), which are defined in data structures but not currently decoded in the CPU execution loop.
- The interaction between debug events, synchronous exceptions, memory accesses, and instruction retirement.
- The field mapping, flag semantics, and filtering contracts required to integrate `TraceEventType::DebugEvent` into the existing 64-byte `TraceEvent` schema without schema version changes or performance regressions.

---

## 2. Detailed Audit of the 17 Subsystem Components

### 2.1 Debugger Class and Public API
- **Location**: [`src/debugger/debugger.h`](file:///D:/WinISO26/WinIOS/src/debugger/debugger.h), [`src/debugger/debugger.cpp`](file:///D:/WinISO26/WinIOS/src/debugger/debugger.cpp)
- **Role**: Host-side inspection and stepping driver.
- **Public API**:
  - `inspect_cpu()`: Formats architectural GPRs (`X0`–`X30`), `SP`, `PC`, and `NZCV` into human-readable strings.
  - `inspect_memory(offset, width)`: Formats guest memory bytes into hex strings.
  - `step()`: Forwards directly to `cpu_.step(memory_)`.
  - `continue_execution(max_instructions)`: Forwards to `cpu_.run(memory_, max_instructions)`.
  - `set_tracing(enabled, sink)`: Legacy callback-based instruction tracing sink.
- **Findings**: The `Debugger` class is a thin host-control wrapper over `CPU` and `Memory`. It does not maintain an internal breakpoint table, software trap dictionary, or guest code patcher. It relies entirely on `CPU` execution semantics.

### 2.2 Breakpoint Data Structures
- **Location**: [`src/cpu/debug_architecture.h`](file:///D:/WinISO26/WinIOS/src/cpu/debug_architecture.h)
- **Structures**:
  - `BreakpointRegister`:
    - `value` (64-bit): `DBGBVR<n>_EL1` (breakpoint virtual address).
    - `control` (32-bit): `DBGBCR<n>_EL1` (breakpoint control register).
    - Field accessors:
      - `enabled()`: bit 0 (`E`).
      - `privilege_mode()`: bits [2:1] (`PMC`: 0=None, 1=EL1, 2=EL0, 3=EL0/EL1).
      - `bas()`: bits [8:5] (`BAS`: 0b1111 for instruction breakpoints).
      - `linked_breakpoint_number()`: bits [19:16] (`LBN`).
      - `breakpoint_type()`: bits [23:20] (`BT`: 0=unlinked address match, 1=linked address match, 3=linked context ID match).
      - `is_linked()`: returns `breakpoint_type() == 1`.
  - Capacity: `kDebugBreakpointCount = 4` slots (reported in `ID_AA64DFR0_EL1.BRPs = 3`).

### 2.3 Software Breakpoint Implementation
- **Location**: [`src/cpu/debug_exceptions.h`](file:///D:/WinISO26/WinIOS/src/cpu/debug_exceptions.h), [`src/cpu/cpu.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp)
- **Findings**:
  - `DebugExceptionKind::SoftwareBreakpoint` is defined in `debug_exceptions.h` with EC `0x3C` (`ExceptionClass::SoftwareBreakpoint`) and 16-bit comment payload.
  - However, in `CPU::step()`, opcode `0xD4200000` (`BRK #imm16`) is **not decoded**. Attempting to execute `BRK` treats it as an undefined instruction or falls through.
  - **Audit Status**: Software breakpoint (`BRK`) instruction execution is **UNSUPPORTED** in the current CPU engine. As mandated by Stage 11 guidelines, no missing architectural functionality will be implemented; `BRK` events will not be emitted unless architectural support is introduced in future phases.

### 2.4 Hardware Breakpoint Implementation
- **Location**: [`src/cpu/debug_architecture.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/debug_architecture.cpp), [`src/cpu/cpu.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp)
- **Findings**:
  - Fully implemented across 4 hardware slots (`DBGBVR0_EL1`..`DBGBVR3_EL1`, `DBGBCR0_EL1`..`DBGBCR3_EL1`).
  - Controlled by global `MDSCR_EL1.MDE` (bit 15). When `MDE == 0`, all hardware breakpoints are globally disabled.
  - Supports linked breakpoint cascades (`BT == 1` linking to secondary slots).
  - Supports privilege filtering (`PMC` matching against `current_el_`).

### 2.5 Instruction Breakpoint Matching
- **Location**: `DebugArchitecture::check_execution_breakpoint(virtual_address, current_el)`
- **Evaluation Order**:
  - Runs in `CPU::step()` at line 4658, **BEFORE** instruction fetch and **BEFORE** instruction execution.
  - Runs after PC alignment validation.
  - Compares `instruction_address` against enabled breakpoint registers.
- **Side Effects**:
  - When matched: produces `DebugEvent` with slot index.
  - Raises a synchronous exception (`ExceptionClass::BreakpointSame` = `0x31` or `ExceptionClass::BreakpointLower` = `0x30`).
  - The instruction is **suppressed**: it is never fetched, never decoded, and never commits register or memory state changes.
  - The instruction does **NOT retire** (`counter_guard.retired` remains false).

### 2.6 Watchpoint Matching
- **Location**: `DebugArchitecture::check_data_watchpoint(virtual_address, access_size, access_type, current_el, instruction_pc)`
- **Registers**: `DBGWVR0_EL1`..`DBGWVR3_EL1`, `DBGWCR0_EL1`..`DBGWCR3_EL1`.
- **Matching Rules**:
  - Monitored by `MDSCR_EL1.MDE` (bit 15).
  - Evaluates `LSC` (Load/Store control: 1=Load, 2=Store, 3=Both).
  - Evaluates `PAC` privilege filter (EL0, EL1, Both).
  - Evaluates Byte Address Select (`BAS`), performing byte-granular overlap checks within an 8-byte aligned region.
  - Evaluates linked watchpoints (`WT == 1`), requiring the executing instruction PC to match `DBGBVR[LBN]`.

### 2.7 Memory Access Hooks Used by Watchpoints
- **Location**: [`CheckedGuestMemory::check_watchpoint`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp#L860-L883)
- **Integration Points**:
  - `CheckedGuestMemory::read()` calls `check_watchpoint(offset, width, AccessType::Read)`.
  - `CheckedGuestMemory::write()` calls `check_watchpoint(offset, width, AccessType::Write)`.
  - Multi-access pair instructions (`STP`, `LDPSW`), SIMD block transfers (`DC ZVA`), and atomic memory instructions route through checked memory calls.
- **Timing & Suppression**:
  - `check_watchpoint` is evaluated **AFTER** virtual-to-physical address translation and alignment checks, but **BEFORE** memory mutation or data payload return.
  - If a watchpoint matches, it constructs `SynchronousException` (EC `0x34`/`0x35`, DFSC `0x22`) and throws `SynchronousExceptionSignal`.
  - This unwinds execution: the memory store is suppressed (memory remains pristine), the register load is suppressed (register remains untouched), and the instruction does **NOT retire**.
  - Because unwinding occurs before `CheckedGuestMemory` completes, **zero** `MemoryRead` or `MemoryWrite` trace events are generated.

### 2.8 CPU Single-Step Behavior
- **Location**: [`src/cpu/cpu.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp#L4340-L4370), `StepCounterGuard`
- **Activation Conditions**:
  - `debug_arch_.software_step_enabled()` (`MDSCR_EL1.SS` == 1).
  - `debug_arch_.monitor_debug_enabled()` (`MDSCR_EL1.MDE` == 1).
  - Debug exception not masked (`DAIF.D == 0` when target EL == current EL).
  - `PSTATE.SS` (bit 21, tracked in `cpu_.pstate_ss_`).
- **Two Distinct Execution Paths**:
  1. **Post-Retirement Single-Step**:
     - Evaluated in `StepCounterGuard` destructor: `if (retired && !cpu.exception_taken_in_step() && ss_active && was_ss)`.
     - The stepped instruction **HAS RETIRED**. Register updates, memory writes, and `InstructionRetired` trace event have already committed.
     - `pstate_ss_` is cleared to `false`.
     - `on_instruction_retired_step(is_exclusive)` raises a synchronous exception (EC `0x32` or `0x33`, ISS holds `EX` bit and DFSC `0x22`).
     - Preferred return address (`ELR_ELx`) is set to the **next instruction address** (`program_counter_`).
  2. **Active-Pending at Instruction Boundary**:
     - Evaluated at line 4635: `if (ss_active_at_start && !was_pstate_ss)`.
     - Condition where step was already pending before execution began.
     - Instruction does **NOT retire**; synchronous exception is raised immediately to target EL.

### 2.9 Debug Exception Generation
- **Location**: [`src/cpu/debug_exceptions.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/debug_exceptions.cpp)
- **Syndrome Generation**:
  - Hardware Breakpoint: EC `0x30` (Lower EL) or `0x31` (Same EL). ISS = 0.
  - Hardware Watchpoint: EC `0x34` (Lower EL) or `0x35` (Same EL). ISS = `(is_write << 6) | 0x22` (DFSC 0x22). FAR = faulting virtual address.
  - Software Step: EC `0x32` (Lower EL) or `0x33` (Same EL). ISS = `(is_exclusive << 6) | 0x22`.

### 2.10 Debug Exception Routing
- **Location**: `determine_debug_target_el(source_el, mdcr_el2, mdcr_el3, hcr_el2)`
- **Routing Rules**:
  - EL3 if source is EL3, or `MDCR_EL3.TDE == 1`.
  - EL2 if source is EL2, or `MDCR_EL2.TDE == 1`, or source is EL0 with `HCR_EL2.TGE == 1`.
  - EL1 otherwise.
- **Vector Offset**: `determine_debug_vector_offset` (0x000 for SP0, 0x200 for SPx, 0x400 for Lower EL AArch64).

### 2.11 Debug State and Halted State
- **Location**: `CPU::is_halted_` (atomic bool in `src/cpu/cpu.h`)
- **Behavior**:
  - When `is_halted_ == true`, `CPU::step()` returns immediately without executing instructions or taking exceptions.
  - Controlled via `CPU::set_halted(bool)`.

### 2.12 Debugger Stop/Resume Operations
- **Findings**:
  - In WinIOS, pausing or resuming execution via host API (`Debugger::step`, `continue_execution`) is a host-control driver operation, not an architectural guest debug exception.
  - Architectural guest debug stops are driven by exception vectors (target EL handler).
  - No synthetic host-control debug events will be conflated with architectural debug exceptions.

### 2.13 Debug Register Implementation
- **Findings**:
  - `MDSCR_EL1`, `DBGBVR<0..3>_EL1`, `DBGBCR<0..3>_EL1`, `DBGWVR<0..3>_EL1`, `DBGWCR<0..3>_EL1`, and `ID_AA64DFR0_EL1` are fully accessible via `MRS`/`MSR` instructions.
  - Access to these registers is already traced via Stage 9 (`TraceEventType::SystemRegisterAccess`, Type 6).

### 2.14 `CPU::step()` Execution Order
1. Check halted state (`is_halted_`).
2. Snapshot `ss_active_at_start` and `was_pstate_ss`.
3. Construct `StepCounterGuard`.
4. Check pending interrupts.
5. Check active-pending single-step at instruction boundary.
6. Validate PC alignment (raise PC alignment fault if unaligned).
7. **Check Hardware Execution Breakpoint** (`check_execution_breakpoint`). If matched: emit `DebugEvent`, raise exception, exit before fetch.
8. Stage 1/Stage 2 MMU instruction fetch translation.
9. Instruction fetch from memory.
10. Decode instruction; initialize `CheckedGuestMemory`.
11. Execute instruction:
    - Data accesses route through `CheckedGuestMemory`.
    - **Check Hardware Data Watchpoint** (`check_watchpoint`). If matched: emit `DebugEvent`, throw `SynchronousExceptionSignal`, unwind execution.
12. Emit memory read/write trace events (if data access succeeded).
13. Update destination registers and PC.
14. Instruction retirement: emit `InstructionRetired` (and `BranchTaken`/`SystemRegisterAccess` if applicable).
15. `StepCounterGuard` destructor:
    - PMU instruction retired counter increment.
    - **Check Single-Step**: If `ss_active && was_ss`, call `on_instruction_retired_step`: emit `DebugEvent`, raise software step exception.

### 2.15 Existing Trace Hooks & Sequence Monotonicity
- All events use `++instruction_trace_sequence_`.
- Event pairing:
  - Breakpoint: `DebugEvent` (seq $N$) -> `ExceptionEntered` (seq $N+1$).
  - Watchpoint: `DebugEvent` (seq $N$) -> `ExceptionEntered` (seq $N+1$).
  - Single-Step: `InstructionRetired` (seq $N$) -> `DebugEvent` (seq $N+1$) -> `ExceptionEntered` (seq $N+2$).

### 2.16 `TraceManager` Event Filtering
- `TraceEventType::DebugEvent` (Type 9) is filtered via `config_.event_types.test(TraceEventType::DebugEvent)`.
- Fully independent of `InstructionRetired`, `BranchTaken`, `ExceptionEntered`, `MemoryRead`, etc.

### 2.17 SPSC Ring Buffer Safety
- CPU execution runs on a single thread submitting sequentially to the preallocated SPSC ring buffer.
- `DebugEvent` emission occurs strictly on the CPU thread, preserving thread-safety and lock-free SPSC guarantees.

---

## 3. Field Mapping for `TraceEventType::DebugEvent` (Type 9)

In the fixed 64-byte `TraceEvent` schema:

| Offset | Field Name | Width | Mapping for DebugEvent |
| :---: | :--- | :---: | :--- |
| **0..7** | `sequence_number` | 8B | Per-core monotonic event sequence number |
| **8..15** | `virtual_time` | 8B | Deterministic physical counter timestamp from `VirtualTime` |
| **16..23** | `pc` | 8B | Relevant guest instruction address (`instruction_address`) |
| **24..31** | `address` | 8B | Monitored/matched guest address (instruction PC for BP/Step, data address for WP) |
| **32..39** | `data` | 8B | Debug slot index (0..3 for BP/WP) or exclusive status (Step) |
| **40..43** | `opcode_or_syndrome` | 4B | Architectural debug ESR syndrome (`ex.esr()`) |
| **44..47** | `flags` | 4B | `IsBreakpoint`, `IsWatchpoint`, `IsSingleStep`, `IsSynchronous`, `IsWrite`, `IsPrivileged`, `HasVirtualAddress` |
| **48..49** | `event_type` | 2B | `TraceEventType::DebugEvent` (9) |
| **50..51** | `core_id` | 2B | CPU core index |
| **52** | `exception_level` | 1B | Privilege level at event detection (`current_el_`) |
| **53** | `address_space` | 1B | `Virtual` (1) when MMU active, `Physical` (2) when MMU disabled |
| **54..55** | `access_size` | 2B | Access width in bytes (4 for instruction BP/Step, transfer width for WP) |
| **56..63** | `reserved` | 8B | Strictly zero (0) |
