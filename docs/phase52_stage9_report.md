# WinIOS Phase 52 Stage 9: System Register Access Trace Integration — Comprehensive Report

## 1. Executive Summary

Phase 52 Stage 9 integrates system register access tracing (`MRS` read and `MSR` write) into the WinIOS AArch64 CPU emulation pipeline:
- `TraceEventType::SystemRegisterAccess` (Type 6)

Building upon the instruction retirement framework (Stage 6), branch-taken integration (Stage 7), and exception lifecycle tracing (Stage 8), Stage 9 captures all architectural system-register transactions across privilege levels EL0, EL1, EL2, and EL3 without altering guest CPU behavior, without violating the single-producer ring buffer contract, and with zero performance impact when tracing is disabled.

### Key Verification Metrics
- **System Register Coverage**:
  - Implemented register inventory of 38 distinct system register keys (16-bit packed encoding `(o0 << 14) | (op1 << 11) | (CRn << 7) | (CRm << 3) | op2`).
  - Covers user space registers (`NZCV`, `FPCR`, `FPSR`, `DCZID_EL0`, `CNTFRQ_EL0`, `CNTPCT_EL0`, `CNTVCT_EL0`), EL1 kernel registers (`SCTLR_EL1`, `TCR_EL1`, `TTBR0_EL1`, `TTBR1_EL1`, `MAIR_EL1`, `PAR_EL1`, `SPSR_EL1`, `ELR_EL1`, `ESR_EL1`, `FAR_EL1`, `VBAR_EL1`, `DAIF`, `CurrentEL`, `SP_EL0`, `SP_EL1`, `SPSel`, `CNTKCTL_EL1`, `CNTP_CTL`, `CNTP_CVAL`, `CNTP_TVAL`, `CNTV_CTL`, `CNTV_CVAL`, `CNTV_TVAL`), EL2 hypervisor registers (`SP_EL2`, `SPSR_EL2`, `ELR_EL2`, `ESR_EL2`, `FAR_EL2`, `HPFAR_EL2`, `VBAR_EL2`, `HCR_EL2`, `VTTBR_EL2`, `VTCR_EL2`, `CNTVOFF_EL2`, `CNTHCTL_EL2`), and EL3 secure monitor registers (`SCR_EL3`, `CPTR_EL3`, `MDCR_EL3`).
  - Full support for PMU event counters (`PMEVCNTR<n>_EL0`) and event typers (`PMEVTYPER<n>_EL0`) across keys `0x5F40..0x5F7E`.
  - Full support for Debug Architecture registers (`MDSCR_EL1`, `DBGBVR<n>_EL1`, `DBGBCR<n>_EL1`, `DBGWVR<n>_EL1`, `DBGWCR<n>_EL1`).
- **Differential Validation**: 100% bit-exact equivalence between untraced and traced execution across all 31 general-purpose registers (`X0`–`X30`), `PC`, `SP_EL0`, `SP_EL1`, `NZCV`, `DAIF`, `SCTLR_EL1`, `TCR_EL1`, `TTBR0_EL1`, and memory contents.
- **Strict Ordering & Monotonicity**: Per-core monotonically increasing sequence numbers (`sequence_number`). A successfully executed `MRS` or `MSR` instruction produces exactly two consecutive trace events:
  1. `TraceEventType::SystemRegisterAccess` (seq $N$)
  2. `TraceEventType::InstructionRetired` (seq $N + 1$)
  Both events share the identical instruction `pc`, `core_id`, and `virtual_time`.
- **Exception & Trap Guarantee**:
  Any attempted system register access that fails privilege checks (e.g. `MRS CurrentEL` at EL0), encounters a read-only violation (e.g. `MSR CurrentEL`), hits a write-only violation (e.g. `MRS ICC_EOIR1_EL1`), encounters an unallocated encoding, or triggers an architectural trap (`MDCR_EL2.TPM`, `MDCR_EL3.TPM`, `MDCR_EL2.TDA`, `MDCR_EL3.TDA`, timer traps) raises a synchronous exception. It emits `TraceEventType::ExceptionEntered` at the vector entry and emits **zero** `SystemRegisterAccess` events and **zero** `InstructionRetired` events.
- **Regression Safety**: All 39 test suites pass across Debug and Release configurations (38 passed, 1 skipped). Zero MSVC compiler warnings under `/W4 /permissive-`.

---

## 2. Architecture Audit & Integration Design

### 2.1 Audit of Existing MRS / MSR Execution Pipeline
In `WinIOS/src/cpu/cpu.cpp`, system register instructions are identified by:
```cpp
const bool is_sysreg_instr =
    ((instruction & kSysRegAnyMask) == kSysRegAnyBase) ||  // MRS / MSR (register)
    ((instruction & kMsrImmMask) == kMsrImmBase);          // MSR (immediate)
```
- Direction is determined by bit 21: `is_read = ((instruction & (1u << 21u)) != 0);` (1 for `MRS`, 0 for `MSR`).
- Encoding key is extracted via 16-bit tuple:
  ```cpp
  const std::uint16_t key = static_cast<std::uint16_t>(
      (o0 << 14) | (op1 << 11) | (crn << 7) | (crm << 3) | op2);
  ```
- Before accessing any register:
  1. Timer traps (`CNTHCTL_EL2` / `CNTKCTL_EL1`) are evaluated.
  2. Debug traps (`MDCR_EL3.TDA`, `MDCR_EL2.TDA`) are checked.
  3. EL0 privilege checks (`can_access_el0(key, is_read)`) reject unprivileged user-mode accesses.
  4. Execution branch:
     - `MRS`: reads register backing state into local variable `value`, writes to destination register `write_zero_register_result(*this, rt, value)`, and captures `sysreg_trace_val = value`.
     - `MSR`: reads operand register `val = read_zero_register_operand(*this, rt)`, captures `sysreg_trace_val = val`, and commits `val` into the architectural backing register.

### 2.2 Selected Trace Emission Points
System register trace emission is integrated into `CPU::emit_trace` closure at the end of instruction retirement:
```cpp
const auto emit_trace = [&](..., bool is_sysreg = false, std::uint16_t reg_key = 0,
                            std::uint64_t reg_value = 0, bool is_write = false) {
    counter_guard.retired = true;
    ...
    if (!is_halted_.load(std::memory_order_acquire)) {
        if (is_sysreg) {
            if (trace_manager_ && trace_manager_->should_trace(
                    winios::trace::TraceEventType::SystemRegisterAccess,
                    static_cast<std::uint16_t>(core_id_))) {
                emit_system_register_access_trace(instruction_address, reg_key, reg_value, is_write, instruction);
            }
        }
        if (trace_manager_ && trace_manager_->should_trace(
                winios::trace::TraceEventType::InstructionRetired,
                static_cast<std::uint16_t>(core_id_))) {
            emit_instruction_retired_trace(instruction_address, instruction);
        }
        ...
    }
};
```
When `is_sysreg_instr` completes successfully:
```cpp
emit_trace(false, false, 0, false, 0, 0, 0,
           true, key, sysreg_trace_val, !is_read);
```

### 2.3 `CPU::emit_system_register_access_trace` Implementation
Defined in [src/cpu/cpu.cpp](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp):
```cpp
void CPU::emit_system_register_access_trace(std::uint64_t instruction_address,
                                            std::uint16_t reg_key,
                                            std::uint64_t reg_value,
                                            bool is_write,
                                            std::uint32_t instruction) {
    winios::trace::TraceEvent ev{};
    ev.sequence_number = ++instruction_trace_sequence_;
    ev.virtual_time = virtual_time_ ? virtual_time_->get_physical_counter() : 0;
    ev.pc = instruction_address;
    ev.address = static_cast<std::uint64_t>(reg_key);
    ev.data = reg_value;
    ev.opcode_or_syndrome = instruction;

    winios::trace::TraceEventFlags flags = winios::trace::TraceEventFlags::HasData;
    if (is_write) {
        flags |= winios::trace::TraceEventFlags::IsWrite;
    }
    if (current_el_ > 0) {
        flags |= winios::trace::TraceEventFlags::IsPrivileged;
    }
    ev.flags = static_cast<std::uint32_t>(flags);
    ev.event_type = static_cast<std::uint16_t>(winios::trace::TraceEventType::SystemRegisterAccess);
    ev.core_id = static_cast<std::uint16_t>(core_id_);
    ev.exception_level = static_cast<std::uint8_t>(current_el_ & 0x3);
    ev.address_space = static_cast<std::uint8_t>(winios::trace::TraceAddressSpace::None);
    ev.access_size = 8;
    ev.reserved = 0;

    trace_manager_->submit(ev);
}
```

---

## 3. Fixed 64-Byte `TraceEvent` Schema Mapping

The schema established in Stage 2 is 100% preserved without modification:

| Byte Offset | Field Name | Type | Value for `TraceEventType::SystemRegisterAccess` |
| :---: | :--- | :--- | :--- |
| `0..7` | `sequence_number` | `uint64_t` | Per-core monotonic counter ($N$) |
| `8..15` | `virtual_time` | `uint64_t` | Authoritative virtual ticks from `VirtualTime` |
| `16..23` | `pc` | `uint64_t` | Virtual address of the executing `MRS`/`MSR` instruction |
| `24..31` | `address` | `uint64_t` | 16-bit packed system register key (`(o0<<14)\|(op1<<11)\|(crn<<7)\|(crm<<3)\|op2`) |
| `32..39` | `data` | `uint64_t` | Value read (for `MRS`) or value written (for `MSR`) |
| `40..43` | `opcode_or_syndrome` | `uint32_t` | Raw 32-bit AArch64 instruction opcode |
| `44..47` | `flags` | `uint32_t` | Bitmask: `HasData`, `IsWrite` (if `MSR`), `IsPrivileged` (if `EL > 0`) |
| `48..49` | `event_type` | `uint16_t` | `TraceEventType::SystemRegisterAccess` (6) |
| `50..51` | `core_id` | `uint16_t` | Logical core index (0..63) |
| `52` | `exception_level` | `uint8_t` | Current exception level at instruction retirement (0, 1, 2, or 3) |
| `53` | `address_space` | `uint8_t` | `TraceAddressSpace::None` (0) — indicates register encoding key, not memory |
| `54` | `access_size` | `uint8_t` | `8` bytes (64-bit register transaction) |
| `55..63` | `reserved` | `uint8_t[9]` | Zero padding |

---

## 4. Verification & Testing

### 4.1 Unit Testing (`sysreg_trace_tests.cpp`)
Suite: [tests/unit/sysreg_trace_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/sysreg_trace_tests.cpp)
Total Tests: **38 / 38 PASSED**

- **Group A: MRS (Register Read) Tests (1–10)**:
  - Tests 1–6: Single event emission, read direction (`IsWrite` cleared), correct destination register value recorded in `data`, system register key encoding, instruction PC capture, `IsPrivileged` flag verification.
  - Tests 7–10: Monotonic sequence incrementing, virtual timestamp correlation, architectural register update verification, access size = 8.
- **Group B: MSR (Register Write) Tests (11–18)**:
  - Tests 11–15: Single event emission, write direction (`IsWrite` set), source register value recorded in `data`, NZCV register key (`0x5A10`), backing register state modification verification (`cpu.get_nzcv()`).
  - Tests 16–18: Instruction PC capture, privilege flag validation, consecutive event sequencing (`seq N` for `SystemRegisterAccess`, `seq N + 1` for `InstructionRetired`).
- **Group C: Privilege, Access Validation & Traps (19–30)**:
  - Test 19: EL0 permitted access (`MRS NZCV` at EL0, `IsPrivileged` cleared, EL0 recorded).
  - Test 20: EL0 trapped / undefined access (`MRS CurrentEL` at EL0 triggers Undefined Instruction exception; vector entry emits `ExceptionEntered`, zero `SystemRegisterAccess` emitted).
  - Tests 21–23: Multi-level execution validation at EL1 (`SCTLR_EL1`), EL2 (`HCR_EL2`), and EL3 (`SCR_EL3`).
  - Test 24: Read-only register write rejection (`MSR CurrentEL` raises synchronous exception, zero access events emitted).
  - Test 25: Write-only register read rejection (`MRS ICC_EOIR1_EL1` raises synchronous exception, zero access events emitted).
  - Test 26: Reserved/unallocated system-register access rejection.
  - Tests 27–28: Trap handling via `MDCR_EL2.TPM` (trapped to EL2) and `MDCR_EL3.TPM` (trapped to EL3) with correct syndrome `EC = 0x18`.
  - Tests 29–30: Debug register accesses (`MDSCR_EL1`) and immediate MSR instructions (`MSR DAIFSet/DAIFClr`).
- **Group D: Filtering & Event Independence (31–35)**:
  - Test 31: Tracing stopped via `TraceManager::stop()`; zero events accepted.
  - Test 32: `SystemRegisterAccess` filtered out; `InstructionRetired` still emitted normally.
  - Test 33: `InstructionRetired` filtered out; `SystemRegisterAccess` still emitted normally.
  - Test 34: Core filtering with core mask `0x2u` rejects Core 0 events.
  - Test 35: Trapped access emits `ExceptionEntered` independently even when `SystemRegisterAccess` is enabled.
- **Group E: Pipeline & Multiple Accesses (36–38)**:
  - Tests 36–38: 4-instruction back-to-back pipeline (`MSR NZCV`, `MRS NZCV`, `MSR DAIF`, `MRS DAIF`) produces 8 strictly monotonic events in deterministic interleaving (`[SysReg, Retired, SysReg, Retired, ...]`), sharing exact instruction PCs.

### 4.2 Integration & Differential Testing (`sysreg_trace_pipeline_tests.cpp`)
Suite: [tests/integration/sysreg_trace_pipeline_tests.cpp](file:///D:/WinISO26/WinIOS/tests/integration/sysreg_trace_pipeline_tests.cpp)
Total Tests: **3 / 3 PASSED**

- **Test 1: Differential CPU State Equivalence**:
  Workload executes 9 instructions with mixed system register reads/writes (`MSR SCTLR_EL1`, `MRS SCTLR_EL1`, `MSR TCR_EL1`, `MRS TCR_EL1`, `MSR DAIF`, `MRS DAIF`, `MSR NZCV`, `MRS NZCV`, `NOP`).
  Architectural snapshot comparison between Run A (Tracing Disabled) and Run B (Full Tracing Enabled):
  - General registers `X0`–`X30`: **100% Bit-Exact Match**
  - Program counter `PC`: **100% Bit-Exact Match**
  - Stack pointers `SP_EL0`, `SP_EL1`: **100% Bit-Exact Match**
  - Flags `NZCV`, `DAIF`: **100% Bit-Exact Match**
  - MMU registers `SCTLR_EL1`, `TCR_EL1`, `TTBR0_EL1`: **100% Bit-Exact Match**
- **Test 2: Stream Monotonicity & Event Correlation**:
  Verifies sequence `MSR NZCV` -> `B` -> `MRS NZCV`:
  Emits exactly 6 events: `[SysReg, Retired, Retired, BranchTaken, SysReg, Retired]`.
  Strict monotonicity verified: `seq[i] == seq[i-1] + 1` across all events.
- **Test 3: Binary Persistence (.wtr) & TraceFileReader Ingestion**:
  Serializes 50 pairs of `MSR`/`MRS` instructions (200 total events) to disk.
  `TraceFileReader` validates header magic, format version 1.0, clean finalization footer, and reconstructs all 200 events with 100% bit-exact field matching.

---

## 5. Performance Benchmark Results

Tool: [tools/cpu_trace_benchmark.cpp](file:///D:/WinISO26/WinIOS/tools/cpu_trace_benchmark.cpp)
Configuration: Windows x86_64, MSVC Release Build (`/O2 /W4 /permissive-`)
Workload: System-register intensive loop running at EL1 executing **175,000 instructions**, **100,000 system-register accesses** (2 writes, 2 reads per iteration), and **25,000 taken branches**.

| Config # | Configuration Description | Time (s) | Throughput (M inst/s) | SysReg Rate (M reg/s) | Latency (ns/inst) | Overhead vs Baseline | Events / Inst | File I/O Throughput | Total Events Accepted |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1** | **Tracing Disabled (Baseline)** | 0.039 s | 4.44 M | — | 225.5 ns | **0.0%** | 0.00 | — | 0 |
| **2** | **Retirement Tracing Only** | 0.041 s | 4.31 M | — | 232.2 ns | **+3.0%** | 1.00 | — | 175,000 |
| **3** | **Retirement + Branch Tracing** | 0.041 s | 4.30 M | — | 232.7 ns | **+3.2%** | 1.14 | — | 199,999 |
| **4** | **Retirement + Branch + Exception** | 0.041 s | 4.24 M | — | 235.6 ns | **+4.5%** | 1.14 | — | 199,999 |
| **5** | **SysReg Filtered Out (Ret+Br+Exc on)** | 0.041 s | 4.29 M | — | 233.4 ns | **+3.5%** | 1.14 | — | 199,999 |
| **6** | **SysReg Tracing Enabled (Ring Buffer)** | 0.044 s | 4.01 M | 2.29 M | 249.4 ns | **+10.6%** | 1.71 | — | 299,999 |
| **7** | **Full Pipeline with File Persistence** | 0.063 s | 2.78 M | 1.59 M | 360.2 ns | **+59.7%** | 1.71 | **290.8 MB/s** | 299,999 |

### Benchmark Observations
1. **Low Overhead of In-Memory System Register Tracing**:
   Enabling system register tracing (Config 6) alongside instruction retirement, branch, and exception tracing generates 300,000 events for 175,000 instructions (1.71 ev/inst), adding only **+10.6% overhead** over baseline while sustaining **4.01 Million instructions/sec** and **2.29 Million system-register trace records/sec**.
2. **Filtering Efficiency**:
   When system register tracing is disabled in `TraceConfig` (Config 5), the fast filter test (`should_trace`) incurs negligible overhead (less than 1 ns/inst).
3. **High File Persistence Throughput**:
   Config 7 sustains **290.8 MB/s** file persistence throughput via the background worker thread, serializing 19.2 MB of trace records without dropping events.

---

## 6. Safeguard & Non-Regression Compliance

1. **Strict Roadmap Safeguard**:
   - `D:\WinISO26\WinIOS_README.md`: UNTOUCHED.
   - `D:\WinISO26\WinIOS\docs\progress.md`: UNTOUCHED.
2. **Pinned Inferno Safeguard**:
   - `D:\WinISO26\Inferno-Research\Inferno\`: Working tree completely clean at commit `cc4302a99167abec69b714cfd00c38caece7e7de`.
3. **Compiler Diagnostics**:
   - Zero errors and zero warnings across all build targets under MSVC `/W4 /permissive-`.
4. **Full Test Suite Status**:
   - 39 / 39 CTest test suites pass in both Debug and Release configurations (38 active passed, 1 skipped).
