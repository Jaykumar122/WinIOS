# WinIOS Campaign 213: Campaign 212 Correctness Audit

**Campaign:** 213  
**Stage:** 213.1  
**Auditor:** WinIOS Autonomous Engineering Core  
**Date:** 2026-11-17  

---

## 1. Syscall ABI Selection & `AutoDetect` Analysis

### Audit Finding:
In `src/kernel/user_syscall_dispatcher.cpp`, the `AutoDetect` branch previously inspected register `X16`:
```cpp
    const std::uint64_t reg16 = cpu.read_register(16);
    if (reg16 != 0) {
        last_used_abi_ = SyscallAbi::DarwinStrict;
        sys_num = reg16;
        ...
    }
```
However, the boolean flag `is_darwin_strict` was initialized to `false` and only set to `true` when `abi_mode_ == SyscallAbi::DarwinStrict`. Consequently, under `AutoDetect`, even if `X16` was detected as Darwin Strict, `record_error()` and `record_success()` fell back to the non-Darwin branch (returning negative errnos instead of setting `PSTATE.C = 1` and returning a positive errno).

### Architectural Correction for Campaign 213:
1. In `AutoDetect`, when `X16 != 0`, `is_darwin_strict` is explicitly set to `true`.
2. Furthermore, in alignment with prompt guidance, **explicit ABI selection is anchored per-process in `BsdProcess`** (`proc.syscall_abi()`, initialized to `SyscallAbi::DarwinStrict`). This guarantees that each process's execution regime is deterministic and does not rely on transient register content heuristics.

---

## 2. Multi-Process Execution Model: Interleaved Scheduling vs Parallel SMP

### Audit Clarification:
The multi-process execution demonstrated in Campaign 212 (Stage 212.20) consists of **preemptive time-sliced interleaved execution** executed on a single virtual CPU core (vCPU 0).
- Context switches are driven by `MultiProcessScheduler` with a 4-tick execution quantum.
- At each context switch, AAPCS64 general-purpose registers and system state (`SP_EL0`, `ELR_EL1`, `SPSR_EL1`) are preserved into the current thread's `ArmTrapFrame`.
- The translation regime is switched by updating `TTBR0_EL1` to the next process's isolated L2 page table PA, followed by an architectural TLB invalidation (`TLBI VMALLE1IS`).
- **Truth Statement:** This is authentic multi-process virtualization with isolated address spaces and cooperative/preemptive time-slicing. It is **NOT** simultaneous parallel SMP execution, as instructions are retired sequentially across time slices on a single virtual CPU pipeline.

---

## 3. Context Preservation & Exception Architecture

- `ArmTrapFrame` (304 bytes packed, 16-byte aligned) accurately preserves `X0..X30`, `SP_EL0`, `ELR_EL1`, `SPSR_EL1`, `ESR_EL1`, `FAR_EL1`.
- Transition from `EL0` to `EL1` is triggered via `SVC #0x80` or `SVC #0`. The synchronous vector base `VBAR_EL1 + 0x400` handles the Lower EL exception trap cleanly.
- Restoring registers and executing `ERET` cleanly returns control to `EL0` at the instruction following `SVC`.

---

## 4. Measurement Methodology: Guest Retirement vs Host Emulation

- **Guest Instruction Retirement:** Measured strictly by counting actual retired ARM64 instructions during CPU execution cycles at `EL0`.
- **System Call Handling:** Counted only when the CPU triggers an exception syndrome `ESR_EL1.EC == 0x15` (`TrappedSVC64`).
- **Host Operations:** Frame presentation, window message pumping, and virtual device MMIO are categorized as host infrastructure (Category C/D) and never conflated with guest-side CPU execution metrics.
