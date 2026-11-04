# WinIOS Phase 52 Stage 11: Debug Features Support Inventory

## 1. Feature Classification Matrix

| Feature | Architectural Category | Implemented in WinIOS | Tracing in Stage 11 | Exact Detection Point | Retirement Behavior | Exception Generated |
| :--- | :--- | :---: | :---: | :--- | :--- | :--- |
| **Hardware Execution Breakpoint** | Architectural AArch64 | **YES** | **YES** | `CPU::step()` at line 4658, before fetch | Suppressed (does NOT retire) | `SynchronousException` (EC `0x30`/`0x31`, ISS 0) |
| **Hardware Data Watchpoint (Load)** | Architectural AArch64 | **YES** | **YES** | `CheckedGuestMemory::check_watchpoint` on Read | Suppressed (unwinds; does NOT retire) | `SynchronousException` (EC `0x34`/`0x35`, ISS `0x22`, FAR=VA) |
| **Hardware Data Watchpoint (Store)** | Architectural AArch64 | **YES** | **YES** | `CheckedGuestMemory::check_watchpoint` on Write | Suppressed (unwinds; does NOT retire) | `SynchronousException` (EC `0x34`/`0x35`, ISS `0x62`, FAR=VA) |
| **Linked Breakpoint Matching** | Architectural AArch64 | **YES** | **YES** | `evaluate_breakpoint_match` via cascade | Suppressed (does NOT retire) | Same as Hardware Execution Breakpoint |
| **Linked Watchpoint Matching** | Architectural AArch64 | **YES** | **YES** | `check_data_watchpoint` with WT=1 & LBN match | Suppressed (does NOT retire) | Same as Hardware Data Watchpoint |
| **Hardware Software Step (Post-Retire)** | Architectural AArch64 | **YES** | **YES** | `StepCounterGuard::~StepCounterGuard()` upon retirement | **RETIRES** (commits state, then takes exception) | `SynchronousException` (EC `0x32`/`0x33`, ISS `(EX<<6)\|0x22`) |
| **Hardware Software Step (Active-Pending)**| Architectural AArch64 | **YES** | **YES** | `CPU::step()` at line 4635 at boundary | Suppressed (does NOT retire) | `SynchronousException` (EC `0x32`/`0x33`, ISS `0x22`) |
| **Software Breakpoint (`BRK`)** | Architectural AArch64 | **NO** | **NO** | Not decoded in `CPU::step()` | N/A | N/A |
| **Host Software Breakpoint Table** | Host Debugger | **NO** | **NO** | Not present in `Debugger` class | N/A | N/A |
| **External Debug Interface** | Architectural AArch64 | **NO** | **NO** | Not implemented | N/A | N/A |
| **Halting Debug Mode** | Architectural AArch64 | **NO** | **NO** | Not implemented | N/A | N/A |
| **Debug Register Access (`MRS`/`MSR`)** | Architectural AArch64 | **YES** | **YES (Stage 9)** | Handled via `SystemRegisterAccess` (Type 6) | Normal retirement | Handled by Stage 9 |
| **Debugger Host Step / Run** | Host Debugger | **YES** | **NO** | Host driver calling `cpu_.step()` | N/A (Driver loop) | N/A |

---

## 2. Detailed Breakdown of Supported Debug Features

### 2.1 Hardware Execution Breakpoints
- **Register Set**: 4 slots (`DBGBVR<0..3>_EL1` and `DBGBCR<0..3>_EL1`).
- **Global Control**: `MDSCR_EL1.MDE` (bit 15).
- **Matching Criteria**:
  - `virtual_address == DBGBVR[i]` (when unlinked `BT == 0`).
  - `privilege_mode_matches(DBGBCR[i].PMC, current_el)`.
  - For linked breakpoints (`BT == 1`): cascade matches against linked slot.
- **Trace Event Attributes**:
  - `event_type`: `TraceEventType::DebugEvent` (9).
  - `flags`: `IsBreakpoint | IsSynchronous | HasVirtualAddress | (current_el > 0 ? IsPrivileged : None)`.
  - `pc`: `instruction_address` (breakpoint address).
  - `address`: `instruction_address`.
  - `data`: `slot_index` (0..3).
  - `opcode_or_syndrome`: ESR syndrome (`0x30` or `0x31` in EC field).
  - `access_size`: 4 bytes.

### 2.2 Hardware Data Watchpoints
- **Register Set**: 4 slots (`DBGWVR<0..3>_EL1` and `DBGWCR<0..3>_EL1`).
- **Global Control**: `MDSCR_EL1.MDE` (bit 15).
- **Matching Criteria**:
  - `load_store_control` (1=Load, 2=Store, 3=Both) matches access direction.
  - `privilege_mode_matches(DBGWCR[i].PAC, current_el)`.
  - Byte Address Select (`BAS`) bitmask matches access byte range.
  - Linked watchpoints (`WT == 1`): `instruction_pc == DBGBVR[LBN]`.
- **Trace Event Attributes**:
  - `event_type`: `TraceEventType::DebugEvent` (9).
  - `flags`: `IsWatchpoint | IsSynchronous | HasVirtualAddress | (is_write ? IsWrite : None) | (current_el > 0 ? IsPrivileged : None)`.
  - `pc`: `instruction_address_` (executing instruction PC).
  - `address`: `address` (monitored memory data address).
  - `data`: `slot_index` (0..3).
  - `opcode_or_syndrome`: ESR syndrome (`0x34` or `0x35` in EC field, DFSC `0x22`, WnR bit).
  - `access_size`: transfer width in bytes (1, 2, 4, 8, 16).

### 2.3 Hardware Single-Step (Software Step)
- **Control Register**: `MDSCR_EL1.SS` (bit 0) and `PSTATE.SS` (bit 21).
- **Condition**: `debug_arch_.software_step_enabled() && debug_arch_.monitor_debug_enabled() && (DAIF.D == 0)`.
- **Event Timing**:
  - Emitted immediately upon instruction retirement before taking the SoftwareStep exception to the target EL.
- **Trace Event Attributes**:
  - `event_type`: `TraceEventType::DebugEvent` (9).
  - `flags`: `IsSingleStep | IsSynchronous | HasVirtualAddress | (current_el > 0 ? IsPrivileged : None)`.
  - `pc`: `stepped_pc` (instruction address of the retired instruction).
  - `address`: `program_counter_` (return address / next instruction PC).
  - `data`: `is_exclusive ? 1 : 0`.
  - `opcode_or_syndrome`: ESR syndrome (`0x32` or `0x33` in EC field, DFSC `0x22`, EX bit).
  - `access_size`: 4 bytes.

---

## 3. Unsupported Features Documentation

1. **Software Breakpoint Instruction (`BRK`)**:
   - In AArch64, `BRK #imm16` (opcode `0xD4200000 | (imm16 << 5)`) generates a Software Breakpoint debug exception (EC `0x3C`).
   - The current WinIOS CPU execution loop does not decode opcode `0xD4200000`.
   - Adhering to the mandate: "Only trace behavior that already exists in the project. Do not implement missing architectural debug functionality simply to expand trace coverage." Software breakpoint trace emission is not implemented.
2. **Host Software Breakpoint Patching**:
   - The `Debugger` class does not maintain a host software breakpoint dictionary. No synthetic software breakpoint events exist.
3. **Physical Address Breakpoints / Context ID Matching**:
   - Not implemented in `DebugArchitecture`.
