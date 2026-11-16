# WinIOS Campaign 209: AIC2 Physical Address Mapping Audit

**Stage:** 209.2 — AIC2 Physical-Address Mapping Correction Audit  
**Campaign:** 209 — Genuine XNU VM Initialization, Physical Page Management & Kernel Memory Services  
**Date:** 2026-11-16  
**Auditor:** Principal MMU & Virtualization Architect  

---

## 1. Objective

To independently decode descriptor `0x0000000220000705ULL` at Index 285 of the Level-2 translation table, determine the complete address translation from guest virtual address `0x23B100000` to physical address space, evaluate compliance with the ARMv8.6-A 16KB translation granule specification, and establish the mathematically and architecturally correct descriptor for the Apple AIC2 MMIO aperture.

---

## 2. Mathematical & Bitfield Decomposition

### 2.1 Target Virtual Address
- Guest Virtual Address: $\text{VA} = \mathtt{0x23B100000}$ ($9,580,871,680\text{ bytes} \approx 8.92\text{ GiB}$)
- Translation Granule: $16\text{ KB}$ ($2^{14}\text{ bytes}$)
- Level-2 Block Size: $2^{25}\text{ bytes} = 32\text{ MB} = \mathtt{0x2000000}$

### 2.2 Table Index & Offset Calculation
$$\text{Level-2 Index} = \lfloor \frac{\mathtt{0x23B100000}}{2^{25}} \rfloor = \mathtt{0x23B100000} \gg 25 = 285 \ (\mathtt{0x11D})$$
$$\text{Block-Internal Offset} = \mathtt{0x23B100000} \pmod{2^{25}} = \mathtt{0x01100000} \ (17,825,792\text{ bytes} = 17\text{ MiB} + 64\text{ KiB})$$

The 32MB block at Index 285 encompasses virtual address range:
$$[\mathtt{0x23A000000}, \mathtt{0x23C000000})$$
Since $\mathtt{0x23A000000} \le \mathtt{0x23B100000} < \mathtt{0x23C000000}$, virtual address `0x23B100000` falls strictly within this block.

---

## 3. Discrepancy in Campaign 208 Descriptor (`0x0220000705`)

### 3.1 Bitfield Extraction
- **Lower Attributes [11:0]:** `0x705`
  - Bits [1:0] = `0b01`: Block descriptor entry (Arm ARM D5-15)
  - Bits [4:2] = `0b001`: `AttrIndx = 1` (Device-nGnRE memory type in MAIR_EL1)
  - Bit 5 = `0`: Non-secure (`NS = 0`)
  - Bits [7:6] = `0b00`: Read/Write at EL1 (`AP[2:1] = 00`)
  - Bits [9:8] = `0b11`: Inner Shareable (`SH = 11`)
  - Bit 10 = `1`: Access Flag set (`AF = 1`)
- **Output Physical Address (OA) [47:25]:**
  $$\text{OA} = \mathtt{0x0220000705} \ \& \ \mathtt{0x0000\_FFFF\_FE00\_0000} = \mathbf{0x0220000000}$$

### 3.2 Translated Physical Address
$$\text{PA}_{\text{translated}} = \text{OA} + \text{Offset} = \mathtt{0x0220000000} + \mathtt{0x01100000} = \mathbf{0x0221100000}$$

### 3.3 Conflict Identification
- **Actual Hardware Base:** Apple AIC2 registers reside at physical address $\mathtt{0x23B100000}$.
- **Translation Error:** Descriptor `0x0220000705` maps `0x23B100000` to $\mathtt{0x0221100000}$.
- **Delta:** $\mathtt{0x23B100000} - \mathtt{0x0221100000} = \mathtt{0x1A000000}$ ($416\text{ MB}$ displacement).
- An MMIO read directed through this mapping accesses unmapped memory rather than AIC2 registers.

---

## 4. Derivation of the Correct Architectural Descriptor

To establish a 1:1 identity mapping where $\text{PA} = \text{VA} = \mathtt{0x23B100000}$:
$$\text{OA}_{\text{required}} = \text{PA} - \text{Offset} = \mathtt{0x23B100000} - \mathtt{0x01100000} = \mathbf{0x023A000000}$$

Combining the 32MB-aligned output address with the required device attributes:
$$\text{Descriptor}_{\text{corrected}} = \mathtt{0x023A000000} \mid \mathtt{0x0000000705} = \mathbf{0x000000023A000705ULL}$$

### 4.1 Verification
- Output Base: `0x023A000000` (Bits [47:25] aligned)
- Offset: `0x01100000`
- Translated PA: `0x023A000000 + 0x01100000 = 0x23B100000` (Exact identity match)
- Memory Type: Device-nGnRE (`AttrIndx = 1`)
- Privileges: EL1 Read/Write (`AP = 0`)
- Access Flag: 1 (no fault on first load/store)
