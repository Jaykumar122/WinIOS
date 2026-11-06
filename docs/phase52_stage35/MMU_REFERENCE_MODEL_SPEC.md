# MMU Reference Model Specification
**Independent Stage 1 Page-Table Translation Algorithm**

---

## 1. Scope & Mathematical Definitions

The `ReferencePageTableWalker` is an isolated, independent implementation of the Armv8-A Stage 1 VMSA (Virtual Memory System Architecture) address translation algorithm for 4KB granules. It shares no state, caching, or code with the production `AArch64Mmu` / `MMU` class.

### 1.1 Address Space Formulation
Let $VA \in [0, 2^{64}-1]$ be a 64-bit virtual address.
The configured address space size is controlled by $T0SZ$:
$$\text{TxSZ} \in [16, 39], \quad \text{PAMax} = 48 \text{ bits}$$
The effective virtual address width is $N = 64 - \text{TxSZ}$.
Any virtual address having bits $[63:N] \ne 0$ (for TTBR0) triggers an **Address Size Fault (Level 0)**.

### 1.2 Top-Byte Ignore (TBI)
If $\text{TCR.TBI0} = 1$ (bit 37):
$$\text{eff\_VA} = VA \,\&\, \text{0x00FFFFFFFFFFFFFF}$$
Otherwise, $\text{eff\_VA} = VA$.

---

## 2. Multi-Level Page-Table Walk (4KB Granule)

Translation decomposes $\text{eff\_VA}[47:0]$ into index fields:

| Level | Bits | Index Width | Granule / Mapping Type |
|---|---|---|---|
| Level 0 | [47:39] | 9 bits (512 entries) | Table Descriptor only |
| Level 1 | [38:30] | 9 bits (512 entries) | Table Descriptor OR 1GB Block Descriptor |
| Level 2 | [29:21] | 9 bits (512 entries) | Table Descriptor OR 2MB Block Descriptor |
| Level 3 | [20:12] | 9 bits (512 entries) | 4KB Page Descriptor |
| Offset | [11:0] | 12 bits | Physical page byte offset |

### 2.1 Descriptor Type Identification
Let $D$ be the 64-bit entry loaded from physical memory:
- If $(D \,\&\, 1) == 0$: **Invalid Descriptor** $\rightarrow$ Translation Fault at current level.
- If $(D \,\&\, 3) == 1$:
  - Level 0: Reserved / Invalid.
  - Level 1: 1GB Block. Base PA = $D \,\&\, \text{0x0000FFFFC0000000}$.
  - Level 2: 2MB Block. Base PA = $D \,\&\, \text{0x0000FFFFFFE00000}$.
  - Level 3: Reserved / Invalid.
- If $(D \,\&\, 3) == 3$:
  - Levels 0, 1, 2: Table Descriptor. Next Table PA = $D \,\&\, \text{0x0000FFFFFFFFF000}$.
  - Level 3: Page Descriptor. Base PA = $D \,\&\, \text{0x0000FFFFFFFFF000}$.

---

## 3. Permission Evaluation Matrix

| AP[2:1] | Privilege (EL1) | Unprivileged (EL0) | Access Allowed |
|---|---|---|---|
| `0b00` | Read / Write | None | Privileged RW only |
| `0b01` | Read / Write | Read / Write | User + Privileged RW |
| `0b10` | Read Only | None | Privileged Read-Only |
| `0b11` | Read Only | Read Only | User + Privileged Read-Only |

### 3.1 Execution Permissions (UXN / PXN)
- `UXN` (bit 54): Unprivileged Execute Never. If set, EL0 instruction fetch raises Permission Fault.
- `PXN` (bit 53): Privileged Execute Never. If set, EL1 instruction fetch raises Permission Fault.
- `WXN` (SCTLR_EL1.WXN, bit 19): Write-implies-XN. If enabled and page is writable ($AP[2] == 0$), both `UXN` and `PXN` are forced to 1.

### 3.2 Access Flag (AF)
Bit 10 of Block/Page descriptor:
- If $\text{AF} == 0$: Raises **Access Flag Fault** at the descriptor's level.

---

## 4. Fault Status Codes (FSC / DFSC / IFSC)

| Fault Condition | FSC Encoding (Level 0..3) |
|---|---|
| Address Size Fault | `0x00` (Level 0), `0x01` (Level 1), `0x02` (Level 2), `0x03` (Level 3) |
| Translation Fault | `0x04` (Level 0), `0x05` (Level 1), `0x06` (Level 2), `0x07` (Level 3) |
| Access Flag Fault | `0x08` (Level 0), `0x09` (Level 1), `0x0A` (Level 2), `0x0B` (Level 3) |
| Permission Fault | `0x0C` (Level 0), `0x0D` (Level 1), `0x0E` (Level 2), `0x0F` (Level 3) |
