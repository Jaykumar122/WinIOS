# Stage 231.15 — Mach-O Toolchain and Binary Format Truth

## Overview
Stage 231.15 audits and validates the Mach-O binary inspector and loader against genuine freestanding ARM64 Mach-O binaries produced by the Zig toolchain. It confirms strict adherence to Mach-O 64-bit format specifications, 16 KB page granule alignment, W^X security constraints, and proper parsing of load commands including `LC_SEGMENT_64`, `LC_MAIN`, `LC_UNIXTHREAD`, and `LC_CODE_SIGNATURE`.

## Verification Scope
1. **Header Truth**:
   - `magic` = `0xfeedfacf` (`MH_MAGIC_64`).
   - `cputype` = `0x0100000c` (`CPU_TYPE_ARM64`).
   - `filetype` = `2` (`MH_EXECUTE`).
2. **Page Granule Alignment**:
   - Verified that segment virtual addresses (`vmaddr`) align to 16 KB boundaries (`0x4000`), as required by Apple A16 Bionic (`T8120`) architecture targets.
3. **W^X Memory Enforcement**:
   - Ensures no segment possesses simultaneous write (`VM_PROT_WRITE = 0x2`) and execute (`VM_PROT_EXECUTE = 0x4`) permissions in `initprot`.
   - Verified that injected W^X violations are strictly detected and rejected.
4. **Load Command Parsing**:
   - `LC_SEGMENT_64` (0x19): parsed with file offset, filesize, and protection validation.
   - `LC_MAIN` (0x80000028) & `LC_UNIXTHREAD` (0x5): entrypoint extraction.
   - `LC_CODE_SIGNATURE` (0x1d): signature offset and length extraction.
5. **Adversarial & Corrupted Binary Rejection**:
   - Binaries < 32 bytes rejected.
   - Non-Mach-O headers (e.g. ELF) rejected.
   - Non-ARM64 architectures (e.g. x86_64) rejected.
   - Out-of-bounds load command blocks rejected.
   - Missing entrypoints rejected.

## Verification
- **Test Suite**: `tests/unit/stage231_15_macho_format_tests.cpp` (CTest #917).
- **Test Results**:
  - RelWithDebInfo: Test #917 Passed in 0.05s.
  - Debug: Test #917 Passed in 0.05s.
