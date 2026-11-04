# WinIOS Phase 52 Stage 9: System Register Architecture Audit

## 1. Overview and Scope

This audit examines the AArch64 system register implementation in WinIOS in preparation for integrating `TraceEventType::SystemRegisterAccess` (type 6) in Phase 52 Stage 9.

The audit covers:
- System register instruction decoding and dispatch (`is_sysreg_instr`, `sysreg_key`).
- `MRS` (read system register to general-purpose register `Xt`).
- `MSR` (write general-purpose register `Xt` to system register).
- Access validation across Exception Levels (EL0, EL1, EL2, EL3).
- Architectural traps: PMU traps (`MDCR_EL2.TPM`, `MDCR_EL3.TPM`), Timer traps (`CNTKCTL_EL1`, `CNTHCTL_EL2`), Debug traps (`MDCR_EL2.TDA`, `MDCR_EL3.TDA`).
- Read-only and write-only register behaviors.
- Backing architectural state and side-effects.
- Trace integration points, field mappings, and ordering with `InstructionRetired`.

---

## 2. Instruction Decoding and Dispatch Analysis

### 2.1 Instruction Group Detection
In [src/cpu/cpu.cpp](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp#L2055):
```cpp
inline bool is_sysreg_instr(std::uint32_t instr) noexcept {
    return ((instr & 0xFFC00000u) == 0xD5000000u)
        && ((instr & 0x00100000u) != 0u);
}
```
- `instr & 0xFFC00000u == 0xD5000000u`: Matches ARMv8 System instructions (`1101 0101 00xx xxxx xxxx xxxx xxxx xxxx`).
- `instr & 0x00100000u != 0u`: Matches bit 20 = 1, selecting the system register MRS/MSR group.
- `direction`: Bit 21 (`L`):
  - `(instr & (1u << 21u)) != 0u` -> `MRS` (Read)
  - `(instr & (1u << 21u)) == 0u` -> `MSR` (Write)
- Target / Source general-purpose register `Rt`: bits [4:0] (`instruction & 0x1Fu`).

### 2.2 System Register Key Packing (`sysreg_key`)
In [src/cpu/cpu.cpp](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp#L1697):
```cpp
inline std::uint16_t sysreg_key(std::uint32_t instruction) noexcept {
    const std::uint32_t op1  = (instruction >> 16u) & 0x7u;
    const std::uint32_t crn  = (instruction >> 12u) & 0xFu;
    const std::uint32_t crm  = (instruction >>  8u) & 0xFu;
    const std::uint32_t op2  = (instruction >>  5u) & 0x7u;
    std::uint32_t o0 = (instruction >> 19u) & 0x1u;
    if (o0 == 0u) {
        const bool is_debug_space = (op1 == 0u && crn == 0u && (op2 == 2u || (op2 >= 4u && op2 <= 7u)));
        if (!is_debug_space) {
            o0 = 1u; // legacy test encoding for op0=3 register
        }
    }
    return static_cast<std::uint16_t>(
        (o0 << 14u) | (op1 << 11u) | (crn << 7u) | (crm << 3u) | op2);
}
```
The 16-bit packed key layout:
- Bit 14: `o0` (0 for `op0=2` debug space, 1 for standard `op0=3` system registers)
- Bits 13:11: `op1` (3 bits)
- Bits 10:7: `CRn` (4 bits)
- Bits 6:3: `CRm` (4 bits)
- Bits 2:0: `op2` (3 bits)

---

## 3. Privilege Validation and Trap Auditing

### 3.1 EL3 and EL2 Privilege Boundaries
1. **EL3 System Registers**: `op1 == 6 || op1 == 7`. If `current_el_ < 3`, raises an Undefined Instruction exception (`ExceptionClass::UnknownReason`).
2. **EL2 System Registers**: `op1 == 4 || op1 == 5`. If `current_el_ < 2`, raises an Undefined Instruction exception.

### 3.2 EL0 Access Permissions (`can_access_el0`)
EL0 execution is restricted to:
- `NZCV`, `FPCR`, `FPSR` (unconditionally allowed for both read and write).
- `DCZID_EL0` (read-only).
- Generic Timers: `CNTFRQ_EL0`, `CNTPCT_EL0`, `CNTVCT_EL0`, `CNTP_*`, `CNTV_*` controlled by `CNTKCTL_EL1` bits [0, 1, 8, 9].
- PMU Registers: Controlled by `PMUSERENR_EL0` enable bits (`EN`, `CR`, `ER`, `SW`).
Any other system register access at EL0 raises an Undefined Instruction exception.

### 3.3 Traps to Higher Exception Levels
1. **PMU Traps**:
   - `MDCR_EL3.TPM` traps PMU register accesses from EL0/EL1/EL2 to EL3 (`EC = 0x18`, Trapped MSR/MRS).
   - `MDCR_EL2.TPM` / `MDCR_EL2.TPMCR` traps PMU accesses from EL0/EL1 to EL2.
2. **Timer Traps**:
   - `virtual_time_->check_trap()` evaluates `CNTKCTL_EL1` and `CNTHCTL_EL2` traps for physical/virtual counter and control registers, generating traps to EL1 or EL2.
3. **Debug Traps**:
   - `MDCR_EL3.TDA` traps debug registers (`MDSCR_EL1`, `DBGBVR*`, `DBGBCR*`, `DBGWVR*`, `DBGWCR*`) to EL3.
   - `MDCR_EL2.TDA` traps debug registers from EL0/EL1 to EL2.

---

## 4. Execution Flow and Event Generation

### 4.1 Successful `MRS` Execution
1. Privilege and trap checks pass.
2. The architectural state is queried once into `std::uint64_t value`.
3. `write_zero_register_result(*this, rt, value)` writes `value` to general-purpose register `Xt` (or discards if `XZR`).
4. `program_counter_` is advanced by 4.
5. Trace event generation:
   - Emits `TraceEventType::SystemRegisterAccess` (type 6):
     - `pc`: Original instruction PC
     - `address`: Packed 16-bit `sysreg_key`
     - `data`: Actual read `value`
     - `opcode_or_syndrome`: Instruction encoding
     - `flags`: `HasData` | (`current_el_ > 0 ? IsPrivileged : None`)
     - `access_size`: `8` (64-bit transaction)
     - `address_space`: `TraceAddressSpace::None`
   - Emits `InstructionRetired` (type 1).

### 4.2 Successful `MSR` Execution
1. Privilege and trap checks pass.
2. `read_zero_register_operand(*this, rt)` reads the supplied value `val` from `Xt`.
3. The register-specific handler writes `val` to the backing state (e.g. `daif_`, MMU, GIC, PMU, Timers).
4. `program_counter_` is advanced by 4.
5. Trace event generation:
   - Emits `TraceEventType::SystemRegisterAccess` (type 6):
     - `pc`: Original instruction PC
     - `address`: Packed 16-bit `sysreg_key`
     - `data`: Supplied written `val`
     - `opcode_or_syndrome`: Instruction encoding
     - `flags`: `HasData` | `IsWrite` | (`current_el_ > 0 ? IsPrivileged : None`)
     - `access_size`: `8`
     - `address_space`: `TraceAddressSpace::None`
   - Emits `InstructionRetired` (type 1).

### 4.3 Trapped or Faulting System Register Access
- If a privilege violation, trap, or unallocated/read-only access occurs:
  - `raise_synchronous_exception(...)` is called.
  - Architectural state transitions to the target vector.
  - `ExceptionEntered` (type 3) is emitted.
  - **Zero** `SystemRegisterAccess` events are emitted.
  - **Zero** `InstructionRetired` events are emitted.

---

## 5. Trace Event Ordering Conclusion

In order to preserve the established pipeline semantics:
1. `SystemRegisterAccess` event (seq `N`) captures the completed register read/write.
2. `InstructionRetired` event (seq `N + 1`) marks instruction retirement.

Both events share the same instruction PC, core ID, exception level, and virtual timestamp, with strictly monotonic sequence numbers.
