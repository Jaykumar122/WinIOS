# WinIOS Stage 208.8: 16KB Page-Table Conformance

## 1. Architectural Granule Specifications (ARMv8-A / ARMv8.1-A)
In 16KB translation granule configurations:
- **Page Size:** 16,384 bytes ($2^{14}$).
- **Page Offset:** Bits `[13:0]` (14 bits).
- **Entries per Table:** 2,048 entries ($2^{11}$), 8 bytes per descriptor $\implies 16\text{KB}$ per table.
- **Index Sizes:** Each translation level consumes 11 bits:
  - Level 3 index: Bits `[24:14]` (11 bits) $\implies$ Maps 16KB pages.
  - Level 2 index: Bits `[35:25]` (11 bits) $\implies$ Maps 32MB blocks or Level 3 tables.
  - Level 1 index: Bits `[46:36]` (11 bits) $\implies$ Maps 64GB blocks or Level 2 tables.
  - Level 0 index: Bit `[47]` (1 bit for 48-bit address space).

## 2. AArch64 Descriptor Format & Bit Fields

```
+----+----+----+-----+----+---+---+---+-------+-----------------------+--------+----+
| UXN| PXN| DBM| Cont| nG | AF| SH| AP| (res) | Output Address (OA)   | AttrIdx| T/B|
| 54 | 53 | 51 | 52  | 11 | 10|9:8|7:6| 5     | [47:14] or [47:25]    | [4:2]  | 1:0|
+----+----+----+-----+----+---+---+---+-------+-----------------------+--------+----+
```

### Key Bit Encodings:
- **Bits `[1:0]` (Type):**
  - `0b00`: Invalid / Fault.
  - `0b01`: Block Descriptor (Levels 1 and 2).
  - `0b11`: Table Descriptor (Levels 0, 1, 2) or Page Descriptor (Level 3).
- **Bits `[4:2]` (AttrIndx):** Memory attributes index into `MAIR_EL1`.
  - `0b000` (Index 0): Normal Memory (Inner/Outer Write-Back Cacheable, Read/Write Allocate: `0xFF`).
  - `0b001` (Index 1): Device Memory (Device-nGnRE: `0x04`).
  - `0b010` (Index 2): Device Memory (Device-nGnRnE: `0x00`).
- **Bits `[7:6]` (AP - Access Permissions):**
  - `0b00`: Read/Write, Privileged only (EL1 RW, EL0 None).
  - `0b01`: Read/Write, Privileged and Unprivileged (EL1 RW, EL0 RW).
  - `0b10`: Read-Only, Privileged only (EL1 RO, EL0 None).
  - `0b11`: Read-Only, Privileged and Unprivileged (EL1 RO, EL0 RO).
- **Bits `[9:8]` (SH - Shareability):**
  - `0b11`: Inner Shareable (Standard for multi-core SMP coherence).
- **Bit `10` (AF - Access Flag):**
  - `0b1`: Accessed (avoids Access Flag faults).
- **Bit `53` (PXN - Privileged Execute-Never):**
  - `0b1`: Cannot be executed at EL1 (Kernel).
- **Bit `54` (UXN - Unprivileged Execute-Never):**
  - `0b1`: Cannot be executed at EL0 (User).

## 3. Dissecting Descriptor `0x0000000000000701`
- `bits [1:0] = 0b01`: Valid Block Descriptor.
- `bits [4:2] = 0b000`: AttrIndx = 0 (Normal Cacheable Memory).
- `bits [7:6] = 0b00`: Kernel Read/Write, User No Access.
- `bits [9:8] = 0b11`: Inner Shareable.
- `bit 10 = 0b1`: Access Flag = 1.
- `bits [47:25] = 0`: Physical Output Address = `0x0` (32MB block identity mapped).
- Low 12 bits: `0b0111_0000_0001` = `0x701`.
- **Verdict:** Fully conformant AArch64 Level-2 32MB block descriptor.
