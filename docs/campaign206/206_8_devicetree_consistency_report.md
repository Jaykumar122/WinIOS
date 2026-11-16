# Stage 206.8: Device-Tree Consistency Audit Report

## 1. Executive Summary
- **Stage:** 206.8 — Device-Tree Consistency
- **Status:** PASS
- **Date:** 2026-11-16
- **Device Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)
- **Format:** Apple Flattened ADTS DeviceTree Format (ADTB) and Flattened DeviceTree (FDT)

---

## 2. Hardware Node Audit & Schema Compliance

| Node Path | Compatible String | Reg Aperture | Driver Role / Status |
|---|---|---|---|
| `/` | `apple,d37ap` | N/A | Root node, defines `model = "iPhone15,4"`, `chip-id = 0x8120`, `board-id = 0x18` |
| `/chosen` | N/A | N/A | Boot arguments, firmware identification, DRAM base/size handoff |
| `/memory` | `memory` | `[physBase, memSize]` | Dynamic physical RAM range for early memory stealer |
| `/cpus` | N/A | N/A | 6 logical CPUs: 2x `apple,everest` (P-cores) + 4x `apple,sawtooth` (E-cores) |
| `/arm-io` | `arm-io` | N/A | Apple SoC IO bus (`#address-cells = 2`, `#size-cells = 2`) |
| `/arm-io/aic` | `apple,aic2` | `[0x23B100000, 0x100000]` | Apple Interrupt Controller v2, 1 MB MMIO aperture |
| `/arm-io/uart0` | `apple,s5l8960x-uart` / `arm,pl011` | `[uart_base, 0x1000]` | Early diagnostic serial console |

---

## 3. Discrepancy Remediations
1. **Added Explicit AIC Aperture:** Previously, `/arm-io/aic` lacked a physical memory `reg` property. The aperture `0x000000023B100000`–`0x000000023B200000` (1 MB) is now explicitly emitted in the serialized Apple DeviceTree blob.
2. **Synthetic vs. Apple Classification:** Generic ARM devices (like generic PL011 or generic GIC) are separated from authentic Apple Silicon bindings (`apple,aic2`, `apple,everest`, `apple,sawtooth`).
