# WinIOS Campaign 205 — Stage 205.16 Report
## Kernel Exception and Fault Recovery Architecture

**Stage:** 205.16  
**Status:** COMPLETE / PASS  
**Date:** 2026-11-16  
**Host:** Windows 11 x64  
**Compiler:** Visual Studio 2026 / MSVC C++20 (`/W4 /WX /permissive-`)  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)

---

### 1. Architectural Summary

In Darwin/XNU kernel execution, early page table faulting, memory probing, copyin/copyout routines, and guard-page enforcement rely on architectural exception handling within EL1:
1. **Synchronous Data Abort Vectoring:**
   - When an unmapped or invalid physical/virtual memory access occurs while executing at EL1 with `SP_EL1` selected (`SPSel = 1`), the CPU takes a synchronous exception to the `CurrentEL_SPx` vector slot (`VBAR_EL1 + 0x200`).
2. **Syndrome and Fault Address Registers:**
   - `FAR_EL1` captures the exact faulting address that triggered the memory translation or external abort.
   - `ESR_EL1` captures the syndrome with Exception Class `EC = 0x25` (Data Abort taken without a change in Exception level).
   - `ELR_EL1` captures the address of the faulting instruction.
3. **Kernel Handler Fixup & Resume Cycle:**
   - The kernel exception handler reads `FAR_EL1`, `ESR_EL1`, and `ELR_EL1`.
   - Modifies `ELR_EL1` (e.g. advancing by 4 bytes or redirecting to a thread recovery label).
   - Executes `ERET` (`0xD69F03E0`), atomically restoring `PSTATE` from `SPSR_EL1` and jumping to the adjusted `ELR_EL1`, resuming normal kernel execution past the fault.

### 2. Implementation Deliverables

- **`tests/unit/stage205_16_kernel_fault_recovery_tests.cpp`:**
  - Configures CPU with `architectural_exceptions = true`, initializes `VBAR_EL1` to `0x2000`, installs `CurrentEL_SPx` vector handler at `0x2200`.
  - Intentionally issues an unmapped load at `0x1000`.
  - Verifies architectural exception delivery to `0x2200`, verifies `FAR_EL1` and `ESR_EL1` (`EC = 0x25`), executes `ELR_EL1 + 4` fixup in the handler, issues `ERET`, and validates successful resumption at `0x1004`.

### 3. Verification & Test Results

- Complete fault capture and resumption cycle verified.
- 100% test pass rate with zero compiler warnings under `/W4 /WX /permissive-`.
