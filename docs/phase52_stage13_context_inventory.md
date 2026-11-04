# WinIOS Phase 52 Stage 13: CPU Context-Change Feature Inventory

## 1. Overview

This document provides a detailed inventory of all architectural execution-context transition mechanisms, exception level switches, stack pointer register-bank selection paths, and exception return operations in WinIOS. It establishes the exact operational status, tracing classification, and trace event generation semantics for `TraceEventType::ContextChanged` (Type 12).

---

## 2. Context-Transition Feature Inventory

| Mechanism / Operation | Source Location | Operational Status | Tracing Classification | Generated Trace Event | Context Reason Code & Metadata |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **SVC from EL0 to EL1** | `src/cpu/cpu.cpp:7018` | Implemented | EL escalation (EL0 $\to$ EL1) | **ContextChanged (Type 12)** | `Reason: ExceptionEntry` (1)<br>`prev_el = 0, new_el = 1`<br>`prev_pc = svc_pc, new_pc = VBAR_EL1 + 0x400` |
| **SVC from EL1 to EL1** | `src/cpu/cpu.cpp:7018` | Implemented | Same-EL trap (EL1 $\to$ EL1) | **ContextChanged (Type 12)** | `Reason: ExceptionEntry` (1)<br>`prev_el = 1, new_el = 1`<br>`prev_pc = svc_pc, new_pc = VBAR_EL1 + 0x200` |
| **HVC from EL1 to EL2** | `src/cpu/cpu.cpp:7028` | Implemented | Hypervisor call (EL1 $\to$ EL2) | **ContextChanged (Type 12)** | `Reason: ExceptionEntry` (1)<br>`prev_el = 1, new_el = 2`<br>`prev_pc = hvc_pc, new_pc = VBAR_EL2 + 0x400` |
| **SMC from EL1/2 to EL3**| `src/cpu/cpu.cpp:7047` | Implemented | Secure Monitor call | **ContextChanged (Type 12)** | `Reason: ExceptionEntry` (1)<br>`prev_el = 1 or 2, new_el = 3`<br>`prev_pc = smc_pc, new_pc = VBAR_EL3 + 0x400` |
| **MMU Fault Trap to EL1** | `src/cpu/cpu.cpp:9153` | Implemented | Synchronous fault entry | **ContextChanged (Type 12)** | `Reason: ExceptionEntry` (1)<br>`prev_el = current, new_el = 1`<br>`prev_pc = fault_pc, new_pc = vector` |
| **Debug Exception Entry** | `src/cpu/cpu.cpp:4049` | Implemented | Breakpoint / Watchpoint / Step | **ContextChanged (Type 12)** | `Reason: ExceptionEntry` (1)<br>`prev_el = current, new_el = target_el`<br>`prev_pc = cur_pc, new_pc = vector` |
| **Timer IRQ Delivery** | `src/cpu/cpu.cpp:4323` | Implemented | Asynchronous exception | **ContextChanged (Type 12)** | `Reason: ExceptionEntry` (1)<br>`prev_el = current, new_el = 1`<br>`prev_pc = interrupted_pc, new_pc = vector` |
| **GIC FIQ Delivery** | `src/cpu/cpu.cpp:4323` | Implemented | Fast interrupt delivery | **ContextChanged (Type 12)** | `Reason: ExceptionEntry` (1)<br>`prev_el = current, new_el = 1`<br>`prev_pc = interrupted_pc, new_pc = vector` |
| **ERET from EL1 to EL0** | `src/cpu/cpu.cpp:6975` | Implemented | Exception return / de-escalation | **ContextChanged (Type 12)** | `Reason: ExceptionReturn` (2)<br>`prev_el = 1, new_el = 0`<br>`prev_pc = eret_pc, new_pc = ELR_EL1` |
| **ERET from EL1 to EL1** | `src/cpu/cpu.cpp:6975` | Implemented | Exception return within EL1 | **ContextChanged (Type 12)** | `Reason: ExceptionReturn` (2)<br>`prev_el = 1, new_el = 1`<br>`prev_pc = eret_pc, new_pc = ELR_EL1` |
| **ERET from EL2 to EL1** | `src/cpu/cpu.cpp:6975` | Implemented | Hypervisor return | **ContextChanged (Type 12)** | `Reason: ExceptionReturn` (2)<br>`prev_el = 2, new_el = 1`<br>`prev_pc = eret_pc, new_pc = ELR_EL2` |
| **ERET from EL3 to EL1/2**| `src/cpu/cpu.cpp:6975` | Implemented | Secure Monitor return | **ContextChanged (Type 12)** | `Reason: ExceptionReturn` (2)<br>`prev_el = 3, new_el = target_el`<br>`prev_pc = eret_pc, new_pc = ELR_EL3` |
| **MSR SPSel, #imm** | `src/cpu/cpu.cpp:7113` | Implemented | Register bank selection | **ContextChanged (Type 12)** | `Reason: RegisterBankSelect` (3)<br>Emitted only when `old_spsel != new_spsel`<br>`prev_spsel, new_spsel` |
| **MSR SPSel, Xt** | `src/cpu/cpu.cpp:8062` | Implemented | Register bank selection | **ContextChanged (Type 12)** | `Reason: RegisterBankSelect` (3)<br>Emitted only when `old_spsel != new_spsel`<br>`prev_spsel, new_spsel` |
| **General ALU / Memory** | `src/cpu/cpu.cpp` | Implemented | Normal instruction execution | **NONE** | No ContextChanged for general instructions |
| **Branches (B, BL, BR)**| `src/cpu/cpu.cpp` | Implemented | Control flow branch | **NONE** | No ContextChanged for standard branches |
| **PSTATE Mask Changes** | `src/cpu/cpu.cpp:7116` | Implemented | `MSR DAIFSet/Clr` | **NONE** | DAIF mask changes alone do not change EL or bank |
| **OS Thread Scheduler** | — | Not Implemented | Unsupported | **NONE** | No simulated OS process/thread context switch |

---

## 3. Context Transition Reasons Enumeration

Grounded strictly in actual WinIOS architectural implementations:

```cpp
enum class ContextChangeReason : std::uint32_t {
    Unknown             = 0,
    ExceptionEntry      = 1, // Exception entry (SVC, HVC, SMC, Abort, Interrupt)
    ExceptionReturn     = 2, // Exception return (ERET)
    RegisterBankSelect  = 3, // Stack pointer register bank selection (MSR SPSel)
    ContextSwitch       = 4  // Architectural context restoration
};
```

---

## 4. Metadata Encoding in Fixed 64-Byte TraceEvent

- `sequence_number`: Monotonic per-core trace counter (`++instruction_trace_sequence_`)
- `virtual_time`: Authoritative physical counter ticks
- `pc`: Source / preceding program counter (`prev_pc`)
- `address`: Destination / new program counter (`new_pc`)
- `data`: Packed bitfield:
  - Bits `[1:0]`: `prev_el` (0..3)
  - Bits `[3:2]`: `new_el` (0..3)
  - Bit `[4]`: `prev_spsel` (0=SP_EL0, 1=SP_ELx)
  - Bit `[5]`: `new_spsel` (0=SP_EL0, 1=SP_ELx)
  - Bits `[11:8]`: `prev_daif`
  - Bits `[15:12]`: `new_daif`
- `opcode_or_syndrome`: `static_cast<std::uint32_t>(reason)`
- `flags`:
  - `HasVirtualAddress`: set (address contains `new_pc`)
  - `HasData`: set (data contains packed context metadata)
  - `IsPrivileged`: set if `new_el > 0`
  - `IsExceptionReturn`: set if `reason == ExceptionReturn`
  - `IsSynchronous`: set if synchronous exception entry
  - `IsInterrupt`: set if asynchronous interrupt entry
- `event_type`: `TraceEventType::ContextChanged` (12)
- `core_id`: target CPU core ID
- `exception_level`: destination Exception Level (`new_el`)
- `address_space`: `TraceAddressSpace::Virtual`
- `access_size`: 0
- `reserved`: 0
