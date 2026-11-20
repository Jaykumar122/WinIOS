# Stage 242.15: AAPCS64 Register, Stack Alignment and ABI Verification

**Campaign**: 242
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Verified Procedure Call Standard for the ARM 64-bit Architecture (AAPCS64):

- **Argument & Return Registers**: Arguments passed in X0-X7; primary return value in X0, secondary return value in X1.
- **Stack Alignment**: SP must be a multiple of 16 bytes (`SP & 0xF == 0`) at public interface boundaries.
- **Callee-Saved Invariants**: Registers X19-X28, Frame Pointer (X29), and Link Register (X30) verified preserved across subroutine calls.
- **Test Target**: `stage242_15_aapcs64_abi_verification_tests` (PASSED).
