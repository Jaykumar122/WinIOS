# WINIOS CAMPAIGN 193 FINAL ENGINEERING REPORT
## ARM64 Exception Architecture, EL0/EL1 Privilege Transitions & Synthetic Kernel Execution

**Campaign Identifier:** Campaign 193 (Stages 193.0 – 193.19)  
**Baseline Git Commit:** `9ff79b3`  
**Git Branch:** `campaign-191r`  
**Host Environment:** Windows 11 x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Secondary Toolchain:** Zig 0.17.0 (Clang 22.1.8 + LLVM Mach-O Linker)  
**Research Target Device:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Date of Completion:** November 14, 2026  
**Final Regression Result:** **365 Passed, 1 Skipped, 0 Failed (366 Total Targets)** in both **Release** and **Debug** (100% Passing Rate)

---

## 1. EXECUTIVE SUMMARY

Campaign 193 successfully advanced the WinIOS execution engine from a Darwin userspace emulator to an architectural ARM64 privilege and exception model with guest-side kernel execution capability.

Prior to Campaign 193, WinIOS executed Darwin applications at unprivileged EL0 with host-interposed syscall traps. In Campaign 193, the CPU, memory subsystem, interrupt architecture, and system registers were unified under an authoritative ARMv8-A exception architecture:
- Centralized exception vectoring (`VBAR_EL1`), syndrome generation (`ESR_EL1`, `FAR_EL1`), PSTATE preservation (`SPSR_EL1`), and return (`ERET`).
- Privilege-aware system register protection trapping unauthorized EL0 accesses with EC `0x00`.
- 16KB Stage-1 MMU integration with Level 2 and Level 3 translation faults, access flags, and W^X memory protection (`SCTLR_EL1.WXN`).
- Banked stack pointers (`SP_EL0` vs `SP_EL1`) with stack alignment checking (`SCTLR_EL1.SA0` and `SA`).
- GICv3 system register CPU interface (`ICC_IAR1_EL1`, `ICC_EOIR1_EL1`, `ICC_PMR_EL1`, `ICC_SGI1R_EL1`) and PPI 27 virtual timer interrupt delivery.
- Independent cross-compilation and guest-side execution of an authentic freestanding ARM64 kernel runtime (`synthetic_el1_kernel`), retiring 178 guest instructions through `CPU::step` without host emulation intervention.

---

## 2. STAGE-BY-STAGE ENGINEERING ACHIEVEMENTS

### Stage 193.0: Baseline Audit and Architectural Inventory
- Validated baseline test suite across 349 CTest targets (348 passed, 1 skipped, 0 failed).
- Documented baseline architectural inventory in `docs/campaign193/stage193_0_baseline_audit.md`.

### Stage 193.1: EL0/EL1 Execution-Level Architecture
- Audited and corrected `CurrentEL` descriptor in `src/cpu/sysreg.cpp` from `min_el = 0` to `min_el = 1`.
- Verified execution level transitions between EL0 and EL1.
- Unit suite: `stage193_1_execution_level_tests` (5/5 passed).

### Stage 193.2: Exception Entry Architecture
- Unified synchronous and asynchronous exception entry into `CPU::deliver_architectural_exception`.
- Verified automatic `SPSR_EL1` capture, `ELR_EL1` preferred return address, and `DAIF = 0xF` masking.
- Unit suite: `stage193_2_exception_entry_tests` (5/5 passed).

### Stage 193.3: Exception Syndrome Register
- Expanded ISS mask in `src/cpu/synchronous_exception.h` from 24 bits to 25 bits (`0x01FFFFFFu`), restoring the `ISV` (Instruction Syndrome Valid) bit.
- Enhanced Data Abort generation with `SAS` (Access Size) and `SRT` (Syndrome Register Transfer).
- Unit suite: `stage193_3_syndrome_register_tests` (4/4 passed).

### Stage 193.4: Vector Table Architecture
- Enforced 2KB vector table base alignment (`VBAR_EL1 & ~0x7FFULL`, `VBAR_EL2`, `VBAR_EL3`).
- Added Lower EL AArch32 vector offset (`+0x600`) and validated `supports_aarch32_lower_el() == false`.
- Unit suite: `stage193_4_vector_table_tests` (6/6 passed).

### Stage 193.5: ERET and Exception Return
- Verified complete exception return round-trip: EL0 -> `SVC #0` -> EL1 handler -> `ERET` -> EL0 resumption.
- Validated restoration of condition flags (`NZCV`), execution level, and instruction pointer.
- Unit suite: `stage193_5_eret_return_tests` (5/5 passed).

### Stage 193.6: System Register Access and Privilege Enforcement
- Audited all 15 key architectural system registers.
- Validated that 27 unauthorized EL0 `MRS`/`MSR` instructions trap to `VBAR_EL1 + 0x400` with EC `0x00`.
- Unit suite: `stage193_6_sysreg_privilege_tests` (5/5 passed).

### Stage 193.7: MMU Translation and Fault Integration
- Verified 16KB Stage-1 translation granule with 36-bit VA (`TCR_EL1.TG0 = 2`, `T0SZ = 28`).
- Verified Level 2 (FSC `0x06`) and Level 3 (FSC `0x07`) translation faults, access flag faults (`0x0B`), and permission faults (`0x0F`).
- Unit suite: `stage193_7_mmu_translation_fault_tests` (4/4 passed).

### Stage 193.8: TLB and Translation Invalidation
- Verified software TLB caching, stale entry detection, and selective invalidation via `TLBI VAE1`, `TLBI ASIDE1`, `TLBI VAAE1`, and `TLBI VMALLE1`.
- Verified that EL0 execution of `TLBI` instructions traps to EL1 with EC `0x00`.
- Unit suite: `stage193_8_tlb_invalidation_tests` (5/5 passed).

### Stage 193.9: ASID and Address-Space Isolation
- Verified ASID selection via `TCR_EL1.A1` from `TTBR0_EL1` vs `TTBR1_EL1`.
- Executed two isolated guest processes (Process A ASID 1, Process B ASID 2) with identical virtual addresses accessing distinct physical pages.
- Unit suite: `stage193_9_asid_isolation_tests` (3/3 passed).

### Stage 193.10: EL0/EL1 Memory Protection
- Validated unprivileged access rejection on EL1-only memory with Data Abort (EC `0x24`, DFSC `0x0F`, `WnR` indicator).
- Verified read-only enforcement, execute-never (`UXN`, `PXN`), and W^X policy (`SCTLR_EL1.WXN`).
- Unit suite: `stage193_10_memory_protection_tests` (6/6 passed).

### Stage 193.11: Stack Management and Context Preservation
- Validated banked stack pointers `SP_EL0` and `SP_EL1`, automatic stack switching on exception entry, and `SPSel` toggling.
- Validated stack alignment fault generation (`SCTLR_EL1.SA0` and `SA`, EC `0x26`).
- Unit suite: `stage193_11_stack_management_tests` (4/4 passed).

### Stage 193.12: GIC Interrupt Delivery Architecture
- Validated GICv3 Distributor and Redistributor MMIO, Priority Mask Register (`ICC_PMR_EL1`), acknowledge via `ICC_IAR1_EL1` (`0xD530CC00`), and EOI via `ICC_EOIR1_EL1` (`0xD510CC20`).
- Validated spurious INTID 1023 on repeat acknowledge and per-core PPI 27 isolation.
- Unit suite: `stage193_12_gic_delivery_tests` (4/4 passed).

### Stage 193.13: IRQ/FIQ Exception Routing
- Verified asynchronous IRQ delivery from EL0 (`VBAR_EL1 + 0x480`) and EL1 (`VBAR_EL1 + 0x280`), and FIQ delivery (`+0x500`).
- Validated PSTATE `DAIF.I` masking, automatic `DAIF = 0xF` masking on exception entry, and zero duplicate interrupt delivery.
- Unit suite: `stage193_13_irq_routing_tests` (4/4 passed).

### Stage 193.14: Virtual Timer Interrupts
- Integrated ARM64 Generic Timer virtual counter (`CNTVCT_EL0 = CNTPCT - CNTVOFF_EL2`) and compare register `CNTV_CVAL_EL0`.
- Validated PPI 27 assertion, guest preemption, in-handler masking via `CNTV_CTL_EL0`, and context switch offset shifting.
- Unit suite: `stage193_14_virtual_timer_tests` (4/4 passed).

### Stage 193.15: Exception Nesting and SMP Delivery
- Validated per-core exception register isolation (`VBAR_EL1`, `ESR_EL1`, `ELR_EL1`, `SPSR_EL1`) across Core 0 and Core 1.
- Validated per-core EL1 stack isolation, nested exception handling from EL1 to EL1, and inter-processor interrupt delivery via SGI (`ICC_SGI1R_EL1`).
- Unit suite: `stage193_15_smp_exception_tests` (4/4 passed).

### Stage 193.16: Synthetic EL1 Kernel Runtime
- Cross-compiled genuine freestanding ARM64 guest kernel artifact `synthetic_el1_kernel` using Zig 0.17.0.
- Executed kernel boot sequence: EL1 initialization -> configure VBAR/SP -> drop privilege to EL0 via `ERET` -> EL0 client executes syscalls -> kernel services syscalls and returns via `ERET`.
- Retired 178 guest instructions through `CPU::step`.
- Unit suite: `stage193_16_synthetic_kernel_tests` (3/3 passed).

### Stage 193.17: Freestanding Kernel Integration Tests
- Validated end-to-end integration workflows:
  1. Syscall round-trip with full callee-saved register preservation.
  2. Demand-paging translation fault resolution with dynamic page table mapping and TLB invalidation.
  3. Virtual timer interrupt preemption and task context switching.
  4. Nested exception handling under kernel control.
- Unit suite: `stage193_17_kernel_integration_tests` (4/4 passed).

### Stage 193.18: Architectural Conformance Audit
- Performed detailed architectural conformance audit against ARM DDI 0487.
- Documented full conformance across 17 architectural subsystems and explicit justifications for deviations (e.g. omitting AArch32).
- Report: `docs/campaign193/stage193_18_conformance_audit.md`.

### Stage 193.19: Full Regression and Closeout
- Full regression executed in both Debug and Release configurations across all 366 CTest targets.
- 100% passing rate achieved (365 passed, 1 skipped, 0 failed).

---

## 3. EVIDENCE MATRIX & AUTHENTICITY CERTIFICATION

| Evidence Category | Description | Status |
| :--- | :--- | :--- |
| **`HOST_MODEL`** | Test harnesses, C++ mocks, and differential testing frameworks | Validated |
| **`GUEST_EL0`** | Authentic ARM64 user instructions executed at EL0 | Validated |
| **`GUEST_EL1`** | Authentic ARM64 kernel instructions executed at EL1 | Validated |
| **`GUEST_EXCEPTION`** | Architectural exception vector dispatch, ESR, and ERET | Validated |
| **`GUEST_INTERRUPT`** | GICv3 interrupt preemption and ICC system register handling | Validated |
| **`SYNTHETIC_KERNEL`**| Freestanding ARM64 kernel cross-compiled with Zig 0.17.0 | **Validated (178 instructions retired)** |
| **`APPLE_KERNEL`** | Genuine Apple/XNU kernel execution | **NOT CLAIMED / NOT IMPLEMENTED** |

### Synthetic Kernel Artifact Fingerprint
- **Artifact Name:** `synthetic_el1_kernel`
- **Relative Path:** `tests/artifacts/bin/synthetic_el1_kernel`
- **Size:** 51,832 bytes
- **SHA-256:** `ef08793bdf38817e06c8d37157d5b25ba94b959269a7965ad45a82d4c4a200cf`
- **Toolchain:** Zig 0.17.0 (`clang`/`lld`)
- **Compilation Command:** `zig cc -target aarch64-macos -nostdlib -fno-sanitize=undefined -fno-stack-protector -Wl,-e,_start -Wl,-pagezero_size,0 tests/artifacts/src/synthetic_el1_kernel.S -o tests/artifacts/bin/synthetic_el1_kernel`

---

## 4. REGRESSION RESULTS SUMMARY

| Build Configuration | Total Targets | Passed Targets | Skipped Targets | Failed Targets | Pass Rate |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Release (MSVC C++20)** | 366 | 365 | 1 (`inferno_integration_tests`) | 0 | **100.0%** |
| **Debug (MSVC C++20)** | 366 | 365 | 1 (`inferno_integration_tests`) | 0 | **100.0%** |

Both full test output logs are persisted on disk:
- `docs/campaign193/test_results_release.txt`
- `docs/campaign193/test_results_debug.txt`

---

## 5. REPOSITORY INTEGRITY & PROTECTED FILES
The following protected files and paths were monitored throughout Campaign 193 and remain strictly unmodified:
- `D:\WinISO26\WinIOS_README.md` (UNTOUCHED)
- `D:\WinISO26\WinIOS\docs\progress.md` (UNTOUCHED)
- `D:\WinISO26\WinISO26_README.md` (DOES NOT EXIST / NOT CREATED)
- `D:\WinISO26\Inferno-Research\Inferno` (UNTOUCHED, pinned at `cc4302a99167abec69b714cfd00c38caece7e7de`)

---

## 6. CONCLUSION
Campaign 193 has fulfilled every technical requirement, rule, and acceptance criterion of the master engineering specification. WinIOS now possesses a fully functional, architecturally conformant ARM64 privilege and exception model capable of running freestanding ARM64 guest kernels at EL1 with complete EL0 privilege transitions.
