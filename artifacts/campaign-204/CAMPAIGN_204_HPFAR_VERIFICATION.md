# WinIOS Campaign 204 — HPFAR and EL2 Architectural Reverification
## Stage 204.1 Deliverable

**Author:** Principal ARM64 Virtualization & MMU Architect  
**Date:** 2026-11-16  
**Reference Document:** ARM Architecture Reference Manual ARMv8, for ARMv8-A architecture profile (ARM DDI 0487)

---

## 1. Architectural Field Definitions (ARM DDI 0487)

Under the ARMv8-A architecture specification:
- **`HPFAR_EL2`** (Hypervisor Physical Fault Address Register, EL2) is defined as follows:
  - **`FIPA` [39:4]**: Fault Intermediate Physical Address. Holds bits **[47:12]** of the faulting intermediate physical address.
  - **`RES0` [3:0]**: Reserved, must be read as 0.
  - **`RES0` [63:40]**: Reserved, read as 0 for configurations with 48-bit intermediate physical addressing.
  *(Note: In ARMv8.2-LPA with 52-bit addressing, bits [47:4] hold IPA[47:12] and bits [63:56] hold IPA[51:48]; WinIOS models the standard 48-bit IPA configuration used by Apple A16 Bionic).*

---

## 2. Mathematical Reconciliation of Reported Extraction Formulas

In Campaign 203, an apparent contradiction arose across documentation descriptions:
1. One narrative stated: *"The field is extracted by shifting right by 4 (`>> 4`)"*.
2. Another narrative stated: *"The IPA is reconstructed by shifting left by 8 (`<< 8`)"*.
3. The reported mask was: `0x000000FFFFFFFFF0ULL`.

### Mathematical Proof of Equivalence:

Let $IPA$ be the 48-bit faulting intermediate physical address.
The hardware constructs $HPFAR\_EL2$ by isolating bits $[47:12]$ and positioning them at bits $[39:4]$:
$$FIPA = (IPA \gg 12) \ \& \ \text{0x0000000FFFFFFFFF}$$
$$HPFAR\_EL2 = FIPA \ll 4 = ((IPA \gg 12) \ll 4) = (IPA \gg 8) \ \& \ \text{0x000000FFFFFFFFF0ULL}$$

When a hypervisor software exception handler receives an EL2 Stage-2 abort, it can reconstruct the base page address of the faulting IPA using either of two methods:

#### Method A: Explicit Two-Step Field Extraction
1. Extract the raw `FIPA` bitfield from bits $[39:4]$:
   $$\text{extracted\_fipa} = (HPFAR\_EL2 \ \& \ \text{0x000000FFFFFFFFF0ULL}) \gg 4$$
2. Since `extracted_fipa` represents $IPA[47:12]$, shift it up by 12 to reconstruct the byte address:
   $$\text{page\_base} = \text{extracted\_fipa} \ll 12$$

#### Method B: Direct One-Step Compound Shift
By combining the right shift by 4 with the left shift by 12 algebraically:
$$\text{page\_base} = ((HPFAR\_EL2 \ \& \ \text{0x000000FFFFFFFFF0ULL}) \gg 4) \ll 12 = (HPFAR\_EL2 \ \& \ \text{0x000000FFFFFFFFF0ULL}) \ll (-4 + 12)$$
$$\text{page\_base} = (HPFAR\_EL2 \ \& \ \text{0x000000FFFFFFFFF0ULL}) \ll 8$$

**Conclusion:** The two descriptions describe the same transformation at different conceptual stages:
- **`>> 4`** describes extracting the architectural `FIPA` bitfield in isolation.
- **`<< 8`** describes the combined net shift directly computing the 48-bit page base address.
Both yield identical bit-exact results across all 48-bit intermediate physical addresses.

---

## 3. Intra-Page Offset Preservation via FAR_EL2

Because $HPFAR\_EL2$ only records $IPA[47:12]$, the intra-page byte offset must be recovered from $FAR\_EL2$:
- When a Stage-1 translation was active, $FAR\_EL2$ holds the faulting Virtual Address ($VA$).
- Because translation mechanisms preserve the low order bits within a translation granule, the lower bits of $FAR\_EL2$ match the lower bits of $IPA$:
  - **4KB Granule:**
    $$IPA = \text{page\_base} \ | \ (FAR\_EL2 \ \& \ \text{0x00000FFF})$$
  - **16KB Granule:**
    Bits $[13:12]$ are preserved in $HPFAR\_EL2$ (since $HPFAR\_EL2$ holds down to bit 12), while bits $[13:0]$ are in $FAR\_EL2$. Thus:
    $$IPA = (\text{page\_base} \ \& \ \sim\text{0x3FFF}) \ | \ (FAR\_EL2 \ \& \ \text{0x00003FFF})$$

---

## 4. Architectural ESR_EL2 Syndrome Validation Matrix

The syndrome report conforms to ARM DDI 0487:

| Fault Type | FSC Code Range | EC Class | IL Bit | WnR (Bit 6) |
|---|---|---|---|---|
| **Translation Fault** | `0x04` + Level (0..3) | `0x24` (Data) / `0x20` (Insn) | 1 (32-bit) | 1 (Write), 0 (Read/Insn) |
| **Access Flag Fault** | `0x08` + Level (0..3) | `0x24` (Data) / `0x20` (Insn) | 1 (32-bit) | 1 (Write), 0 (Read) |
| **Permission Fault** | `0x0C` + Level (0..3) | `0x24` (Data) / `0x20` (Insn) | 1 (32-bit) | 1 (Write), 0 (Read) |

This behavior is verified with zero deviations in `stage204_1_hpfar_el2_reverification_tests.cpp`.

---

## 5. Verification Status

**Stage 204.1 Status: PASS**
