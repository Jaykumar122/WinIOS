# WinIOS Campaign 205 — Stage 205.4 Engineering Report
## Dedicated Kernel Image Parser & Mach-O Header Validation

**Date:** November 16, 2026  
**Campaign:** 205  
**Stage:** 205.4  
**Deliverables:**
- `src/loader/kernel_image_parser.h`
- `src/loader/kernel_image_parser.cpp`
- `tests/unit/stage205_4_kernel_image_parser_tests.cpp`

---

### 1. Implementation Overview
Stage 205.4 builds `KernelImageParser`, a dedicated Mach-O 64-bit kernel inspection and validation layer tailored for Darwin/XNU kernel binaries and freestanding ARM64 kernel fixtures.

Key capabilities:
1. **Header Validation:** Enforces 64-bit Mach-O magic (`0xFEEDFACF`), CPU type (`CPU_TYPE_ARM64 = 0x0100000C`), valid ARM64 subtypes (`ALL`, `V8`, `ARM64E`), and file type (`MH_EXECUTE = 2`).
2. **Segment Inspection:** Parses `LC_SEGMENT_64` commands (`__TEXT`, `__DATA`, `__DATA_CONST`, `__BOOTDATA`, `__LINKEDIT`), validates `initprot` and `maxprot`, and computes preferred base address and total VM footprint.
3. **Entrypoint Discovery:** Supports entry extraction via `LC_UNIXTHREAD` (reading the `pc` field in `arm_thread_state64_t` at offset 272) or `LC_MAIN` (`entryoff`), with fallback to the virtual base of `__TEXT`.
4. **Adversarial & Boundary Validation:**
   - 16KB granule page alignment (`0x4000`) for all segment virtual addresses.
   - Integer overflow detection for 64-bit `vmaddr + vmsize` and `fileoff + filesize`.
   - File range boundary checking preventing reads past raw buffer capacity.
   - Strict overlapping segment detection via virtual address sorting.
   - Zero-sized load command detection preventing infinite loop vulnerabilities.

---

### 2. Verification & Test Results
Test suite `stage205_4_kernel_image_parser_tests` executed with 9 focused test cases:
- Test 1: Valid ARM64 Mach-O kernel parsing with `__TEXT`, `__DATA`, and `LC_UNIXTHREAD` entry.
- Test 2: Truncated header rejection (`TruncatedHeader`).
- Test 3: Invalid magic rejection (`InvalidMagic`).
- Test 4: Unsupported CPU type rejection (`UnsupportedCpuType`).
- Test 5: Unsupported file type rejection (`UnsupportedFileType`).
- Test 6: Corrupted load commands rejection (`CorruptedLoadCommands`).
- Test 7: Misaligned segment rejection (`MisalignedSegment`).
- Test 8: Overlapping segments rejection (`OverlappingSegments`).
- Test 9: Segment out of bounds rejection (`SegmentOutOfBounds`).

**All 9 tests PASSED.** Zero warnings under `/W4 /WX /permissive-`.

**Stage 205.4 Status: PASSED**
