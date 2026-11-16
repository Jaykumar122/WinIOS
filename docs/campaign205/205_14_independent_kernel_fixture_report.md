# WinIOS Campaign 205 — Stage 205.14 Report
## Independent ARM64 Kernel-Entry Fixture & Exception Cycle

**Stage:** 205.14  
**Status:** COMPLETE / PASS  
**Date:** 2026-11-16  
**Host:** Windows 11 x64  
**Compiler:** Visual Studio 2026 / MSVC C++20 (`/W4 /WX /permissive-`)  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)

---

### 1. Architectural Summary

Stage 205.14 establishes an independent, freestanding ARM64 Mach-O kernel fixture (`IndependentKernelFixtureBuilder`) that executes genuine ARMv8-A instructions on the WinIOS virtual CPU:
1. **EL1 Kernel Entry Point:**
   - Entered with `current_el = 1` (EL1h) and active `SP_EL1`.
   - Reads `boot_args` from `X0` and `X19` using `LDR` instructions (`physBase`, `memSize`).
   - Validates stack pointer via `MOV X3, SP`.
2. **Vector Table Initialization:**
   - Programs `VBAR_EL1` with the address of the fixture's 2048-byte aligned vector table (`MSR VBAR_EL1, X4`).
3. **Privilege Drop via Exception Return (`ERET`):**
   - Prepares `ELR_EL1` pointing to the EL0 user code entry point (`MSR ELR_EL1, X5`).
   - Configures `SPSR_EL1` for EL0t 64-bit execution state (`MSR SPSR_EL1, X6`).
   - Executes `ERET` (`0xD69F03E0`), transitioning execution level from EL1 to EL0 and updating `PC` to `ELR_EL1`.
4. **EL0 System Call (`SVC`):**
   - Executes user code at EL0 and issues `SVC #0` (`0xD4000001`).
   - CPU traps into EL1, saves `PSTATE` to `SPSR_EL1`, saves return `PC` to `ELR_EL1`, populates `ESR_EL1` with syndrome `EC = 0x15` (SVC in AArch64), and jumps to `VBAR_EL1 + 0x400` (Lower EL AArch64 Synchronous vector).
5. **EL1 Exception Handler & Completion Flag:**
   - Exception handler at `VBAR_EL1 + 0x400` reads `ESR_EL1` (`MRS X7, ESR_EL1`).
   - Writes the 64-bit completion magic `0xCAFEFEEDC001CAFEULL` to target memory (`STR X9, [X10]`).
   - Successfully concludes execution with `WFI` (`0xD503207F`).

### 2. Implementation Deliverables

- **`src/loader/independent_kernel_fixture.h` & `.cpp`:**
  - Implements `IndependentKernelFixtureBuilder::build_fixture` producing a fully compliant 64-bit Mach-O binary containing genuine ARM64 machine code for entry, stack check, sysreg setup, `ERET`, `SVC`, vector handling, and completion notification.
- **`tests/unit/stage205_14_independent_kernel_fixture_tests.cpp`:**
  - Integrates with `KernelBootCoordinator`, configures architectural exceptions, steps through the full 19-instruction cycle, and validates bit-exact state transitions and memory flags.

### 3. Verification & Test Results

- Executed 19 architectural instructions across EL1 -> EL0 -> EL1.
- Both `observed_el0` and `returned_to_el1` verified true.
- Memory target `0x3800` contained exact magic `0xCAFEFEEDC001CAFEULL`.
- 100% test pass rate with zero compiler warnings under `/W4 /WX /permissive-`.
