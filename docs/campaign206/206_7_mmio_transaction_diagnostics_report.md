# Stage 206.7: MMIO Transaction Diagnostics Report

## 1. Executive Summary
- **Stage:** 206.7 — MMIO Transaction Diagnostics
- **Status:** PASS
- **Date:** 2026-11-16
- **Component:** `winios::device::MmioDiagnostics` (`src/device/mmio_diagnostics.h/.cpp`)
- **Capability:** Detailed transaction capture for all memory bus and peripheral transactions, tracking physical address, access width, read/write direction, originating PC, CPU privilege level, Apple Silicon device classification, and fault status.

---

## 2. Diagnostics Architecture
The diagnostics subsystem records every non-DRAM transaction and external abort:
- **Heuristic Classification:** Automatically identifies address ranges:
  - `0x000000000`–`0x07FFFFFF`: DRAM
  - `0x000090000` / `0x235200000`: Synthetic / Apple S5L8960X UART
  - `0x08000000`–`0x080FFFFF`: Standard ARM GICv3
  - `0x23B100000`–`0x23B1FFFFF`: Apple AIC2 Interrupt Controller
  - `0x23B000000`–`0x23B07FFFF`: Apple PMGR Power Manager
  - `0x27C000000`–`0x27C07FFFF`: Apple ANS2 Storage
  - `0x280000000`–`0x28007FFFF`: Apple DART / IOMMU
  - Beyond mapped ranges: `UnmappedFault`

---

## 3. Integration & Output Formats
1. **JSON Output (`to_json()`):** Structured schema recording timestamp cycle, PC, PA, width, direction, value, privilege, classification, and details.
2. **Markdown Table (`to_markdown_report()`):** Human-readable diagnostic breakdown for inspection in campaign deliverables.
