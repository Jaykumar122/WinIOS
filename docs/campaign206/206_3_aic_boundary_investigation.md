# Stage 206.3: Exact AIC Boundary Investigation Report

## 1. Executive Summary
- **Stage:** 206.3 — Exact AIC Boundary Investigation
- **Status:** PASS
- **Date:** 2026-11-16
- **Faulting Instruction PC:** `0x0000000000001048`
- **Faulting Instruction Hex:** `0xF940037A` (`ldr x26, [x27]`)
- **Faulting Physical Target Address:** `0x000000023B100000`

---

## 2. Instruction Decomposition & Disassembly
```
Address     Encoding    Disassembly                     Semantic Context
0x0000103C  D280001B    movz x27, #0x0, lsl #0          Setup low 16 bits of AIC MMIO base
0x00001040  F2A7621B    movk x27, #0x3B10, lsl #16      Insert middle 16 bits (0x3B100000)
0x00001044  F2C0005B    movk x27, #0x2, lsl #32         Insert upper bits (0x200000000 -> 0x23B100000)
0x00001048  F940037A    ldr x26, [x27]                  Attempt read from Apple AIC base register
```

### Technical Attributes of Faulting Access:
- **Instruction Type:** ARM64 A64 Load Register (Unscaled / Unsigned Immediate, offset = 0)
- **Access Direction:** Read (Load)
- **Access Width:** 64-bit (8 bytes)
- **Source Register:** `X27` (`0x000000023B100000`)
- **Destination Register:** `X26`
- **Addressing Mode:** Base register direct (`[Xn]`)
- **Execution Privilege:** Exception Level 1 (EL1h, `CurrentEL = 0b01`, `SPSel = 1`)

---

## 3. Architectural Syndrome & Register State at Boundary

### 3.1 Exception Syndrome Registers:
- **`FAR_EL1` (Fault Address Register):** `0x000000023B100000` (Exact byte target)
- **`ESR_EL1` (Exception Syndrome Register):**
  - Raw Value: `0x96000010`
  - `EC` (Exception Class, bits [31:26]): `0x25` (Data Abort taken without a change in Exception Level)
  - `IL` (Instruction Length, bit 25): `1` (32-bit instruction trapped)
  - `ISS` (Instruction Specific Syndrome, bits [24:0]): `0x000010`
    - `ISV` (bit 24): `0` (Instruction syndrome not valid for external abort)
    - `DFSC` (bits [5:0]): `0b010000` (`0x10` = Synchronous External Abort, not on translation table walk)
- **`ELR_EL1` (Exception Link Register):** `0x0000000000001048` (Return PC points to the faulting instruction)
- **`SPSR_EL1` (Saved Program Status Register):** `0x00000000000003C5` (EL=1, SP=SP_EL1, D=1, A=1, I=1, F=1)

### 3.2 General-Purpose Register Snapshot:
- `X0`: `0x0000000000020000` (Physical address of `boot_args`)
- `X19`: `0x0000000000020000` (Preserved `boot_args` pointer)
- `X20`: `0x0000000080000000` (Queried `MPIDR_EL1` — Boot CPU cluster 0, core 0)
- `X21`: `0x0000000000000000` (`physBase` extracted from `boot_args.physBase`)
- `X22`: `0x0000000000400000` (`memSize` extracted from `boot_args.memSize` — 4 MB)
- `X23`: `0x0000000000024000` (`deviceTreeP` extracted from `boot_args.deviceTreeP`)
- `X24`: `0x0000000000090000` (UART base address)
- `X25`: `0x0000000000000055` (Character `'U'`)
- `X26`: `0x0000000000000000` (Unmodified prior to load)
- `X27`: `0x000000023B100000` (AIC MMIO target address)
- `SP_EL1`: `0x0000000000084000` (Valid 16KB aligned boot stack pointer)
