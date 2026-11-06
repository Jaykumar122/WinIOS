# WinIOS — Phase 52, Stage 38: Baseline Audit Report

## Architectural Exception Delivery, GICv3 Integration, Virtual Timers & Deterministic SMP Interrupt Architecture

**Date:** 2026-11-06  
**Project Root:** `D:\WinISO26\WinIOS\`  
**Research Root:** `D:\WinISO26\Inferno-Research\`  
**Pinned Inferno Commit:** `cc4302a99167abec69b714cfd00c38caece7e7de`  
**Language/Compiler:** C++20 / Visual Studio 18 2026, x64

---

## 1. Executive Summary

Phase 52, Stage 37 hardened MMU translation-table walking (1GB/2MB block alignment, descriptor type checks), implemented `CPACR_EL1` FP/SIMD access control with architectural exception traps, established the isolated `DarwinCompatibilityRegistry`, and bound new state into `.wsnap v2` snapshots and deterministic replay. All 96 CTest suites passed in Release (95 passed, 1 skipped) and Debug.

Stage 38 focuses on consolidating architectural exception delivery, GIC interrupt routing, virtual timers, CPU exception entry/return, and deterministic SMP scheduling into an independently validated, deterministic interrupt and exception architecture.

This baseline audit evaluates the existing implementation of all 20 required architectural areas, catalogs supported features, identifies gaps and architectural defects, and defines the precise implementation scope for Stage 38.

---

## 2. Comprehensive Architectural Audit (20 Areas)

### 2.1 CPU Exception State
- **Source Files:** [`src/cpu/cpu.h`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.h), [`src/cpu/cpu.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp).
- **Supported Functionality:** Modeled execution levels EL0 (unprivileged user), EL1 (privileged OS kernel), EL2 (hypervisor), and EL3 (secure monitor). Banked stack pointers `SP_EL0`, `SP_EL1`, `SP_EL2`, `SP_EL3` with `PSTATE.SP` selection (`sp_sel_`).
- **Defects & Gaps:** Exception delivery logic was fragmented across six discrete entry points (`raise_synchronous_exception`, `raise_el2_exception`, `raise_el3_exception`, `raise_asynchronous_exception`, `raise_el2_asynchronous_exception`, `raise_el3_asynchronous_exception`). Vector calculation, SPSR encoding, and DAIF masking were duplicated, increasing risk of divergence.

### 2.2 PSTATE and DAIF
- **Source Files:** [`src/cpu/cpu.h`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.h), [`src/cpu/cpu.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp).
- **Supported Functionality:** `PSTATE` condition flags `NZCV` (bits [31:28]), interrupt mask bits `DAIF` (D: bit 3 / mask 0x8, A: bit 2 / mask 0x4, I: bit 1 / mask 0x2, F: bit 0 / mask 0x1), `IL` (Illegal execution state), and `SS` (Software step). Masking and unmasking via `MSR DAIFSet, #imm4` and `MSR DAIFClr, #imm4`.
- **Defects & Gaps:** `SmpInterruptRouter::deliver_next()` directly invoked `cpu.raise_asynchronous_exception()` without checking `PSTATE.DAIF`, potentially bypassing guest IRQ/FIQ masking if called outside `CPU::check_pending_interrupts()`.

### 2.3 SPSR_EL1 and ELR_EL1
- **Source Files:** [`src/cpu/cpu.h`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.h), [`src/cpu/cpu.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp), [`src/cpu/smp_core_state.h`](file:///D:/WinISO26/WinIOS/src/cpu/smp_core_state.h).
- **Supported Functionality:** `encode_spsr()` packs NZCV, DAIF, originating EL, SPsel, IL, and SS into SPSR format. `elr_el1_` stores preferred return address.
- **Defects & Gaps:** Preferred return address conventions differ between synchronous traps (e.g., SVC returns instruction + 4, while aborts and instruction traps return instruction PC). ERET must restore these registers with strict state validation.

### 2.4 ESR_EL1 and FAR_EL1
- **Source Files:** [`src/cpu/synchronous_exception.h`](file:///D:/WinISO26/WinIOS/src/cpu/synchronous_exception.h), [`src/cpu/cpu.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp).
- **Supported Functionality:** `SynchronousException::esr()` formats `EC` (bits [31:26]), `IL` (bit 25), and `ISS` (bits [23:0]). `FAR_EL1` holds faulting virtual addresses for data aborts, instruction aborts, watchpoints, and PC alignment. For IRQ and FIQ, `ESR_EL1` is 0.

### 2.5 VBAR_EL1 and Exception Vectors
- **Source Files:** [`src/cpu/cpu.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp).
- **Supported Functionality:** Vector base address register `VBAR_EL1` holds table base (2KB aligned).
- **Vector Offsets:**
  - Current EL with SP0: Sync `+0x000`, IRQ `+0x080`, FIQ `+0x100`, SError `+0x180`.
  - Current EL with SPx: Sync `+0x200`, IRQ `+0x280`, FIQ `+0x300`, SError `+0x380`.
  - Lower EL using AArch64: Sync `+0x400`, IRQ `+0x480`, FIQ `+0x500`, SError `+0x580`.
  - Lower EL using AArch32: Sync `+0x600`, IRQ `+0x680`, FIQ `+0x700`, SError `+0x780`.
- **Defects & Gaps:** Offset calculations were scattered and duplicated across functions.

### 2.6 ERET Behavior
- **Source Files:** [`src/cpu/cpu.cpp:7346-7398`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp#L7346-L7398).
- **Supported Functionality:** Decodes `ERET` (encoding `0xD69F03E0u`), validates privilege (forbidden at EL0), checks `SPSR.M` field (valid: `0x00` EL0t, `0x04` EL1t, `0x05` EL1h).
- **Defects & Gaps:**
  - When `!valid_m || target_el > current_el_`: if `architectural_exceptions_` was enabled, NZCV and DAIF were mutated and `il_` was set to true, but execution continued with mismatched state; if disabled, a host C++ `std::runtime_error` was thrown.
  - An invalid ERET must follow the architectural ARMv8 / Inferno specification (`illegal_return`): restore NZCV and DAIF, set `PSTATE.IL = 1`, restore PC from `ELR`, preserve Current EL and SP, and prevent partial architectural corruption. Misaligned target PCs must fault deterministically.

### 2.7 Synchronous Exception Generation
- **Source Files:** [`src/cpu/synchronous_exception.h`](file:///D:/WinISO26/WinIOS/src/cpu/synchronous_exception.h).
- **Supported Categories:** `Unknown` (0x00), `FpSimdAccessTrap` (0x07), `IllegalExecutionState` (0x0E), `SvcAArch64` (0x15), `HvcAArch64` (0x16), `SmcAArch64` (0x17), `TrappedMsrMrs` (0x18), `InstructionAbortLower/Same` (0x20/0x21), `PcAlignment` (0x22), `DataAbortLower/Same` (0x24/0x25), `BreakpointLower/Same` (0x30/0x31), `WatchpointLower/Same` (0x34/0x35).

### 2.8 MMU Translation Faults
- **Source Files:** [`src/cpu/mmu.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/mmu.cpp).
- **Supported Functionality:** Levels 0–3 translation faults, permission faults (AP[2:1], UXN, PXN), 1GB/2MB block alignment enforcement, and address size faults (>48 bits).

### 2.9 FP/SIMD Access Traps
- **Source Files:** [`src/cpu/cpu.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp), [`src/cpu/sysreg.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/sysreg.cpp).
- **Supported Functionality:** `CPACR_EL1` (0x4082) with FPEN bits [21:20] access control (0b00 traps EL0 & EL1, 0b01 traps EL0, 0b10 traps EL0 & EL1, 0b11 enables both). EC = 0x07, ESR = 0x1E000000u.

### 2.10 GIC Distributor (GICD)
- **Source Files:** [`src/cpu/gicv3.h`](file:///D:/WinISO26/WinIOS/src/cpu/gicv3.h), [`src/cpu/gicv3.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/gicv3.cpp).
- **Base/Size:** `0x08000000`, 64KB.
- **Supported Registers:** `GICD_CTLR`, `GICD_TYPER`, `GICD_IGROUPR[4]`, `GICD_ISENABLER[4]`, `GICD_ICENABLER[4]`, `GICD_ISPENDR[4]`, `GICD_ICPENDR[4]`, `GICD_ISACTIVER[4]`, `GICD_ICACTIVER[4]`, `GICD_IPRIORITYR[128]`, `GICD_ICFGR[8]`, `GICD_IROUTER[96]`.

### 2.11 GIC Redistributor (GICR)
- **Source Files:** [`src/cpu/gicv3.h`](file:///D:/WinISO26/WinIOS/src/cpu/gicv3.h), [`src/cpu/gicv3.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/gicv3.cpp).
- **Base/Stride:** `0x080A0000`, 128KB stride per virtual core.
- **CRITICAL DEFECT IDENTIFIED:** `GICv3::kMaxCores` in `gicv3.h` was hardcoded to `4`:
  ```cpp
  static constexpr std::size_t kMaxCores = 4;
  RedistributorState redistributors_[kMaxCores]{};
  CpuInterfaceState icc_[kMaxCores]{};
  ```
  However, Stages 28–30 expanded `SmpTopologyConfig::kMaxSupportedCores` to `8`. When operating in 8-core SMP topologies, `smp_cluster.cpp` clamped GIC core count to 4:
  ```cpp
  const std::size_t count = std::max(std::size_t{1}, std::min(num_cores, GICv3::kMaxCores));
  gic_.set_core_count(count);
  ```
  Consequently, virtual cores 4–7 were accessing redistributor and CPU interface state clamped to core 0! `GICv3::kMaxCores` must be expanded to 8.

### 2.12 ICC System Registers
- **Source Files:** [`src/cpu/gicv3.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/gicv3.cpp), [`src/cpu/cpu.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp).
- **Supported Registers:**
  - `ICC_PMR_EL1` (0x4230): Priority Mask Register (RW).
  - `ICC_IAR0_EL1` / `ICC_IAR1_EL1` (0x4640 / 0x4660): Interrupt Acknowledge Registers (RO).
  - `ICC_EOIR0_EL1` / `ICC_EOIR1_EL1` (0x4641 / 0x4661): End of Interrupt Registers (WO).
  - `ICC_HPPIR0_EL1` / `ICC_HPPIR1_EL1` (0x4642 / 0x4662): Highest Priority Pending Registers (RO).
  - `ICC_BPR0_EL1` / `ICC_BPR1_EL1` (0x4643 / 0x4663): Binary Point Registers (RW).
  - `ICC_CTLR_EL1` (0x4664): Control Register (RW).
  - `ICC_IGRPEN0_EL1` / `ICC_IGRPEN1_EL1` (0x4666 / 0x4667): Interrupt Group Enable Registers (RW).
  - `ICC_SGI1R_EL1` (0x465D): SGI Generation Register (WO).
  - `ICC_DIR_EL1` (0x4659): Deactivate Interrupt Register (WO).
  - `ICC_RPR_EL1` (0x465B): Running Priority Register (RO).

### 2.13 SGI/PPI/SPI Routing
- **Source Files:** [`src/cpu/smp_interrupt_router.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/smp_interrupt_router.cpp), [`src/cpu/gicv3.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/gicv3.cpp).
- **Classification:**
  - SGIs (INTID 0–15): Banked per core, targeted via core bitmask.
  - PPIs (INTID 16–31): Banked per core. PPI 23 (PMU), PPI 26 (CNTHP), PPI 27 (CNTV), PPI 30 (CNTP).
  - SPIs (INTID 32–127): Shared peripheral interrupts, routed via `GICD_IROUTER` affinity target.

### 2.14 Interrupt Priority and Masking
- **Source Files:** [`src/cpu/gicv3.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/gicv3.cpp).
- **Semantics:** Lower numeric value = higher architectural priority. Priority comparison against `ICC_PMR_EL1` and running priority stack (`active_priorities`). Deterministic tie-breaking for equal priority: lowest INTID selected first.

### 2.15 Virtual Timer Implementation
- **Source Files:** [`src/cpu/virtual_time.h`](file:///D:/WinISO26/WinIOS/src/cpu/virtual_time.h), [`src/cpu/virtual_time.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/virtual_time.cpp).
- **Supported Registers:** `CNTFRQ_EL0`, `CNTPCT_EL0`, `CNTVCT_EL0`, `CNTP_TVAL_EL0`, `CNTP_CVAL_EL0`, `CNTP_CTL_EL0`, `CNTV_TVAL_EL0`, `CNTV_CVAL_EL0`, `CNTV_CTL_EL0`, `CNTHP_TVAL_EL2`, `CNTHP_CVAL_EL2`, `CNTHP_CTL_EL2`.
- **Semantics:** Monotonic counter comparison: `istatus = enable && (counter >= cval)`. IRQ line asserted when `enable && istatus && !imask`.

### 2.16 Virtual-Time Source
- **Source Files:** [`src/cpu/virtual_time.h`](file:///D:/WinISO26/WinIOS/src/cpu/virtual_time.h).
- **Semantics:** Architectural 24 MHz monotonic physical counter. No host wall-clock time dependency. Counter advances strictly via instruction step, replay stream, or deterministic idle jump.

### 2.17 SMP Scheduler
- **Source Files:** [`src/cpu/smp_scheduler.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/smp_scheduler.cpp).
- **Semantics:** Deterministic single-host-thread scheduler running virtual cores in fixed round-robin order with bounded instruction quanta (`quantum_instructions_`). Quanta truncate deterministically on exception, WFI, WFE, or halt.

### 2.18 Snapshot Serialization
- **Source Files:** [`src/snapshot/smp_snapshot_manager.cpp`](file:///D:/WinISO26/WinIOS/src/snapshot/smp_snapshot_manager.cpp), [`src/snapshot/snapshot_serializer.cpp`](file:///D:/WinISO26/WinIOS/src/snapshot/snapshot_serializer.cpp).
- **Sections:** Metadata, CoreDirectory, CoreArchitecturalState, SharedMemory, Scheduler, ExclusiveMonitor, InterruptRouter, DeviceState (0x18), Integrity.

### 2.19 Canonical Cluster Hashing
- **Source Files:** [`src/cpu/smp_scheduler.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/smp_scheduler.cpp), [`src/cpu/smp_core_state.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/smp_core_state.cpp).
- **Semantics:** 64-bit FNV-1a hash across all per-core architectural states, shared memory, and cross-core interrupt router pending queues.

### 2.20 SNDL Event Recording and Replay
- **Source Files:** [`src/sndl/sndl_v2_recorder.cpp`](file:///D:/WinISO26/WinIOS/src/sndl/sndl_v2_recorder.cpp), [`src/replay/smp_replay_coordinator.cpp`](file:///D:/WinISO26/WinIOS/src/replay/smp_replay_coordinator.cpp).
- **Events:** QuantumStart, CoreSelected, Exception, InterruptDelivered, CounterRead, DmaOperation.

---

## 3. Defects, Inconsistencies & Remediation Scope

| Item | Architectural Area | Existing Defect / Gap | Stage 38 Remediation Plan |
|:---:|:---|:---|:---|
| **D1** | GICv3 Core Capacity | `GICv3::kMaxCores` was 4, while `SmpTopology` supports 8 cores. Cores 4–7 lacked independent GIC state. | Expand `GICv3::kMaxCores` to 8; instantiate 8 independent Redistributors and CPU Interfaces. |
| **D2** | Exception Delivery Dispatch | Delivery logic scattered across 6 disparate entry points with duplicated vector calculations. | Implement centralized `ArchitecturalExceptionDelivery` subsystem and unified entry path. |
| **D3** | ERET Failure Semantics | Invalid ERET partially mutated NZCV/DAIF or threw C++ exceptions instead of taking architectural illegal return. | Implement architectural `illegal_return`: restore NZCV/DAIF, set `PSTATE.IL = 1`, restore PC from ELR, preserve Current EL and SP. |
| **D4** | DAIF Masking Bypass | `SmpInterruptRouter::deliver_next` could deliver interrupts while DAIF was masked. | Enforce DAIF masking strictly at delivery boundary; keep masked interrupts pending in GIC/router. |
| **D5** | Reference Model GIC Parity | `ReferenceGicModel` lacked complete 8-core PPI isolation and timer PPI integration. | Extend `ReferenceGicModel` with full 8-core support, timer PPI evaluation, and priority preemption. |
| **D6** | Test Suite Completeness | Existing tests lacked comprehensive coverage for ERET state integrity, GIC priority preemption, and timer PPI isolation. | Implement 14 dedicated Stage 38 conformance, differential, fuzzing, and endurance test suites. |

---

## 4. Verification Strategy

1. **Centralized Architectural Exception Delivery:** Validate unified vector selection across SP0, SPx, and Lower EL AArch64 for all supported exception classes.
2. **ERET State Integrity:** Verify that valid returns restore full state and invalid returns strictly execute `illegal_return` without partial state corruption.
3. **8-Core GICv3 Scalability:** Verify independent interrupt routing, masking, and acknowledgement across all 8 virtual cores.
4. **Virtual Timer PPI Isolation:** Verify that Core N's timer asserts PPI 27 on Core N only, with zero cross-core leakage.
5. **Deterministic Replay & Snapshot Parity:** Verify that pending interrupts and timer states survive snapshot restore with identical canonical cluster hashes.
6. **Full Suite Execution:** Verify 100% pass across all 96+ CTest suites in Release and Debug configurations.
