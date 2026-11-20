# Stage 243.14: AAPCS64 Register Classification & LR Preservation

**Campaign**: 243
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Corrected and documented AAPCS64 register preservation rules:

- **Callee-Saved**: X19–X28 and X29 (Frame Pointer).
- **Link Register (X30)**: Corrected classification — X30 holds the subroutine return address and is clobbered by nested `BL`/`BLR` calls. It must be saved on stack (`STP X29, X30, [SP, #-16]!`) by non-leaf functions.
- **Stack Alignment**: Enforced 16-byte alignment (`SP % 16 == 0`).
- **Test Target**: `stage243_14_aapcs64_lr_preservation_tests` (PASSED).
