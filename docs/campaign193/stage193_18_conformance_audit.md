# Stage 193.18 — Architectural Conformance Audit

## 1. Conformance Scope & Architectural Reference
This architectural conformance audit reviews the ARM64 privilege and exception model implemented in WinIOS Campaign 193 against the official ARM Architecture Reference Manual ARMv8, for ARMv8-A architecture profile (ARM DDI 0487I.a / DDI 0487K.a):

| Subsystem | Architectural Specification | Implemented Conformance Status | Verification Suite |
| :--- | :--- | :--- | :--- |
| **Execution Levels** | ARM DDI 0487 D1.1 (EL0 Userspace, EL1 Kernel) | **CONFORMANT** | `stage193_1_execution_level_tests` |
| **Exception Entry** | ARM DDI 0487 D1.10 (Centralized delivery, PSTATE save) | **CONFORMANT** | `stage193_2_exception_entry_tests` |
| **Syndrome Register** | ARM DDI 0487 D13.2.39 (ESR_EL1 25-bit ISS, EC, ISV) | **CONFORMANT** | `stage193_3_syndrome_register_tests` |
| **Vector Table** | ARM DDI 0487 D1.10.1 (VBAR_EL1 2KB align, 16 vectors) | **CONFORMANT** | `stage193_4_vector_table_tests` |
| **Exception Return** | ARM DDI 0487 C6.2.82 (ERET restoration, PC, PSTATE) | **CONFORMANT** | `stage193_5_eret_return_tests` |
| **Sysreg Privilege** | ARM DDI 0487 D13.2 (EL0 MSR/MRS trap with EC 0x00) | **CONFORMANT** | `stage193_6_sysreg_privilege_tests` |
| **MMU & Faults** | ARM DDI 0487 D5 (16KB granule, L2/L3 faults, FSC) | **CONFORMANT** | `stage193_7_mmu_translation_fault_tests` |
| **TLB Invalidation** | ARM DDI 0487 D5.10 (TLBI VAE1, ASIDE1, VMALLE1) | **CONFORMANT** | `stage193_8_tlb_invalidation_tests` |
| **ASID Isolation** | ARM DDI 0487 D5.5 (TTBR0/1 ASID, TCR.A1, TLB tagging) | **CONFORMANT** | `stage193_9_asid_isolation_tests` |
| **Memory Protection** | ARM DDI 0487 D5.4 (AP[2:1], UXN, PXN, SCTLR.WXN) | **CONFORMANT** | `stage193_10_memory_protection_tests` |
| **Stack Management** | ARM DDI 0487 D1.10.2 (Banked SP, SPSel, SA0/SA check) | **CONFORMANT** | `stage193_11_stack_management_tests` |
| **GICv3 Delivery** | ARM IHI 0069H (Distributor, Redistributor, ICC sysregs) | **CONFORMANT** | `stage193_12_gic_delivery_tests` |
| **IRQ/FIQ Routing** | ARM DDI 0487 D1.10.3 (DAIF masking, vectors +0x480/+0x280) | **CONFORMANT** | `stage193_13_irq_routing_tests` |
| **Virtual Timer** | ARM DDI 0487 D11 (CNTVCT, CNTV_CVAL, CNTV_CTL, PPI 27) | **CONFORMANT** | `stage193_14_virtual_timer_tests` |
| **SMP & Nesting** | ARM DDI 0487 D1.10 (Per-core state, nested SPx vectors) | **CONFORMANT** | `stage193_15_smp_exception_tests` |
| **Synthetic Kernel** | ARM DDI 0487 (Freestanding EL1 boot, ERET, guest runtime) | **CONFORMANT** | `stage193_16_synthetic_kernel_tests` |
| **Integration Flow** | End-to-end multi-privilege system integration | **CONFORMANT** | `stage193_17_kernel_integration_tests` |

---

## 2. Detailed Architectural Subsystem Analysis

### 2.1 Execution Levels & Privilege Demotion / Escalation
- `CurrentEL` register returns bits [3:2] indicating execution level (`0b00` for EL0, `0b01` for EL1).
- EL0 is restricted from reading or writing privileged registers.
- Privilege transitions:
  - Synchronous exception (SVC, trap, fault): instantaneous escalation to EL1. PSTATE saved in `SPSR_EL1`, PC saved in `ELR_EL1`.
  - Exception return (`ERET`): restores execution level, PSTATE flags, and instruction pointer atomically from `SPSR_EL1` and `ELR_EL1`.

### 2.2 Syndrome Registers & Exception Reporting
- `ESR_EL1` layout conforms to Section D13.2.39:
  - Bits [31:26]: Exception Class (EC). Validated for `0x00` (Trapped MRS/MSR), `0x15` (SVC in AArch64), `0x20` (Instruction Abort Lower), `0x21` (Instruction Abort Same), `0x24` (Data Abort Lower), `0x25` (Data Abort Same), `0x26` (SP Alignment Fault).
  - Bit [25]: Instruction Length (IL = 1 for 32-bit A64 instruction).
  - Bits [24:0]: Instruction Specific Syndrome (ISS). Masked with `0x01FFFFFFu`.
  - For Data Aborts: ISS includes `ISV` (bit 24), `SAS` (bits [23:22]), `SSE`/`SRT` (bits [20:16]), and `WnR` (bit 6).
- `FAR_EL1`: captures accurate faulting virtual address on all instruction and data aborts.

### 2.3 Vector Table Architecture
- `VBAR_EL1` strictly masks lower 11 bits (`& ~0x7FFULL`), guaranteeing 2048-byte architectural alignment.
- Vector offset computation matches Section D1.10.1:
  - Current EL with SP0: Synchronous (`+0x000`), IRQ (`+0x080`), FIQ (`+0x100`), SError (`+0x180`).
  - Current EL with SPx: Synchronous (`+0x200`), IRQ (`+0x280`), FIQ (`+0x300`), SError (`+0x380`).
  - Lower EL using AArch64: Synchronous (`+0x400`), IRQ (`+0x480`), FIQ (`+0x500`), SError (`+0x580`).
  - Lower EL using AArch32: Synchronous (`+0x600`), IRQ (`+0x680`), FIQ (`+0x700`), SError (`+0x780`).

### 2.4 MMU, ASID and W^X Protection
- Stage-1 translation supports 16KB granule with 36-bit VA (`TCR_EL1.TG0 = 2`, `T0SZ = 28`).
- Fault Status Codes (FSC) accurately distinguish Translation fault level 2 (`0x06`), Level 3 (`0x07`), Access flag fault (`0x0B`), and Permission fault (`0x0F`).
- ASID selection honors `TCR_EL1.A1` selecting between `TTBR0_EL1` and `TTBR1_EL1`.
- `SCTLR_EL1.WXN` (bit 19) treats all writable pages as execute-never, raising IFSC `0x0F` on attempted execution.

### 2.5 Stack Alignment Checking
- `SCTLR_EL1.SA0` (bit 4) enforces 16-byte stack pointer alignment for EL0 memory accesses.
- `SCTLR_EL1.SA` (bit 3) enforces 16-byte alignment for EL1 memory accesses.
- Any misaligned stack pointer memory operation generates SP Alignment Fault with EC `0x26`.

### 2.6 Interrupt Routing & Generic Timers
- Non-secure Group 1 interrupts vector to `+0x480` (Lower EL) or `+0x280` (Current EL).
- Group 0 interrupts (FIQ) vector to `+0x500`.
- System registers `ICC_IAR1_EL1` (`0xD530CC00`), `ICC_EOIR1_EL1` (`0xD510CC20`), and `ICC_PMR_EL1` (`0xD5184230`) fully implement the GICv3 CPU interface.
- PPI 27 virtual timer delivers interrupts on counter expiration against `CNTV_CVAL_EL0`.

---

## 3. Explicit Deviations and Architectural Simplifications

1. **AArch32 Execution State at Lower ELs:**
   - **Deviation:** `CPU::supports_aarch32_lower_el()` returns `false`.
   - **Justification:** Conforms to authentic Apple A16 Bionic (`T8120`) hardware design (and ARMv9 / Apple Silicon 64-bit only cores), which completely omits 32-bit AArch32 hardware decode and execution logic.
2. **Simplified EL2/EL3 Traps in Single-OS Mode:**
   - **Simplification:** In Darwin userspace and freestanding kernel test modes, EL2 (hypervisor) and EL3 (secure monitor) traps are disabled or bypassed when running EL0/EL1 execution flows.
   - **Justification:** Matches the architectural requirements for single-OS execution and OS kernel testing.
3. **Monotonic Virtual Clock Advancement:**
   - **Simplification:** Virtual counter progression is stepped deterministically via `advance_physical_counter` or cycle advancement in test harnesses rather than requiring asynchronous wall-clock CPU interrupts.
   - **Justification:** Guarantees 100% deterministic, reproducible regression testing across all host environments.
