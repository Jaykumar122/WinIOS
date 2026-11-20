# Stage 241.5: Mach-O Header and Load-Command Validation

## 1. Loader Hardening Scope
Implemented `tests/unit/stage241_5_macho_loader_hardening_tests.cpp` to verify robust parser validation against adversarial or malformed inputs:

1. **Header Validation**:
   - Rejects binary smaller than 32 bytes (`sizeof(mach_header_64)`).
   - Rejects invalid magic (expected `0xFEEDFACF`).
   - Rejects non-ARM64 CPU types (e.g. x86_64, ARMv7).
2. **Load Command Stream Boundary Verification**:
   - Rejects binary when `sizeofcmds` claims more bytes than file size.
   - Rejects load command with `cmdsize < 8`.
   - Rejects load command whose `cmdsize` exceeds remaining load command buffer.
3. **Segment Sanity**:
   - Rejects segments whose virtual addresses are not aligned to 16 KB page boundaries.
   - Rejects segments with simultaneous Write and Execute permissions (`W^X` policy).

## 2. Test Results
- All adversarial and malformed parser tests pass 100%.
