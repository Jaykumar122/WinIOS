# Stage 206.4: Architectural Boundary Classification Report

## 1. Executive Summary
- **Stage:** 206.4 — Architectural Boundary Classification
- **Status:** PASS
- **Date:** 2026-11-16
- **Definitive Classification:** **Unmapped MMIO Access on Virtual Memory Bus / Missing Apple Interrupt Controller (AIC) Interconnect**
- **Secondary Factor:** Platform topology mismatch between ARM GICv3 and Apple Silicon AICv2/v3

---

## 2. Hypothesis Testing & Architectural Diagnosis

| Hypothesis | Test Methodology & Evidence | Result | Classification Reason |
|---|---|---|---|
| **H1: Unsupported Instruction Opcode** | Inspected opcode `0xF940037A` (`LDR X26, [X27]`). Decoder handles ARMv8-A Load Register. Exception class is `EC = 0x25` (Data Abort), NOT `EC = 0x00` (Unknown/Undefined). | **REJECTED** | Instruction is valid ARM64 A64 ISA; failure is in memory subsystem, not instruction decode. |
| **H2: Unsupported Apple System Register** | Inspected faulting instruction. The instruction is a memory load `LDR`, not `MRS`/`MSR` to an implementation-defined system register (`S3_...`). | **REJECTED** | The access is a memory-mapped physical address access, not a system register access. |
| **H3: Incorrect DeviceTree Node** | Evaluated generated Apple DeviceTree. The DeviceTree is passive memory metadata; even with a valid `aic` node, a memory bus read requires an active hardware responder. | **REJECTED** | DeviceTree is data; the fault occurred on a hardware MMIO read before XNU driver binding. |
| **H4: Unmapped MMIO on Virtual Memory Bus** | Analyzed `CPU::translate_access_pages` and `check_address_size_and_range` in `src/cpu/cpu.cpp` (lines 1206-1212). The CPU solely queries `cpu_.get_gic().is_mmio_address(address)` for non-RAM physical ranges. | **CONFIRMED** | Address `0x23B100000` lies outside 4 MB DRAM and outside GICv3 range. CPU generates `FaultStatus::SynchronousExternal` (EC=0x25 Data Abort). |
| **H5: Apple Silicon Platform Topology Dependency** | Apple Silicon SoCs (T8103 / T8110 / T8120) do not contain standard ARM GICv3 IP. They incorporate proprietary Apple Interrupt Controller (AIC) IP mapped at physical address `0x23B100000`. | **CONFIRMED** | Authentic Apple Silicon execution requires an AIC-compliant MMIO responder at `0x23B100000`. |

---

## 3. Engineering Remediation Path
To allow genuine Darwin/XNU early bootstrap code to proceed beyond the boundary:
1. The CPU memory translation and access pipeline must connect to an interrupt controller abstraction or `VirtualDeviceBus`.
2. Physical address range `0x23B100000`–`0x23B200000` must be routed to `device::AppleAIC`.
3. Read operations to AIC registers (`AIC_INFO`, `AIC_WHOAMI`) must return authentic Apple AICv2 register semantics rather than aborting.
4. Detailed MMIO transaction diagnostics must log all subsequent accesses to trace further platform dependencies.
