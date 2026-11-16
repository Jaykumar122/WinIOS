# Stage 206.6: Interrupt-Controller Abstraction Audit Report

## 1. Executive Summary
- **Stage:** 206.6 — Interrupt-Controller Abstraction Audit
- **Status:** PASS
- **Date:** 2026-11-16
- **Architecture Identified:** Decoupling of generic ARM GICv3 from Apple Silicon AICv2/v3
- **Primary Finding:** The historical CPU implementation had ARM GICv3 tightly coupled into the memory read/write path. To support genuine Apple Silicon (T8120) execution, an explicit `IInterruptController` abstraction is defined to prevent accidental GICv3 behavior from being masqueraded as Apple AIC.

---

## 2. Architectural Comparison: GICv3 vs. Apple AICv2

| Architectural Attribute | ARM Generic Interrupt Controller (GICv3) | Apple Interrupt Controller v2 (AICv2) |
|---|---|---|
| **Specification Authority** | Arm IHI 0069G (Public standard) | Apple Inc. Proprietary Silicon Architecture |
| **SoC Implementation** | Cortex-A / Neoverse / Generic ARM64 | Apple A14–A16, M1–M3 (`T8103`, `T8110`, `T8120`) |
| **MMIO Base Address** | `0x08000000` (GICD) / `0x080A0000` (GICR) | `0x23B100000` (1 MB unified aperture) |
| **Routing Topology** | Distributor + Per-Core Redistributor + CPU I/F | Central routing table + per-core banked registers |
| **Core Identification** | `MPIDR_EL1` affinity routing | Banked `AIC_WHOAMI` register at offset `+0x2000` |
| **Event Lifecycle** | `ICC_IAR1_EL1` / `ICC_EOIR1_EL1` sysregs | Memory-mapped `AIC_EVENT` pop and EOI write |
| **IPI Signaling** | `ICC_SGI1R_EL1` system register | MMIO `AIC_IPI_SEND` bitmask register |
| **Timer Routing** | PPI 27 / 30 via GICR | Banked `kEventTypeTimer` event in `AIC_EVENT` |

---

## 3. Abstraction Interface: `IInterruptController`
To prevent mixing semantics, `winios::cpu::IInterruptController` formalizes the contract:
- `kind()`: Returns `InterruptControllerKind::GicV3` or `InterruptControllerKind::AppleAic2`.
- `is_mmio_address(pa)`: Tests if physical address falls within the controller's aperture.
- `read_mmio(pa, width, core_id)`: Dispatches controller-specific register read.
- `write_mmio(pa, value, width, core_id)`: Dispatches controller-specific register write.
- `has_pending_interrupt(core_id)`: Asserts CPU IRQ/FIQ line.
- `reset()`: Resets hardware state.

This ensures that when a target runs in Apple Silicon research mode (`iPhone15,4` / `T8120`), only `AppleAIC` responds at `0x23B100000`, and zero GICv3 behavior is exposed.
