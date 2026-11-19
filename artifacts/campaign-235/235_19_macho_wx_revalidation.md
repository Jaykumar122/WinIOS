# Stage 235.19: Mach-O Mapping, ASLR Slide Invariance & W^X Revalidation

## Executive Summary

Stage 235.19 revalidates the ARM64 freestanding Mach-O loading pipeline (`winios::loader::UserspaceMachOLoader`, `winios::loader::AppInspector`, and `winios::memory::DarwinVmSpace`). The test suite demonstrates strict enforcement of W^X memory permissions, 4 GB PAGEZERO guard band unmapping, 16 KB page granule alignment, and ASLR slide distance invariance.

## Key Properties Verified

1. **W^X Enforcement (Static & Runtime)**:
   - `AppInspector::inspect_binary` analyzes all `LC_SEGMENT_64` commands and rejects any binary containing simultaneous Write and Execute permissions (`(initprot & (VM_PROT_WRITE | VM_PROT_EXECUTE)) == (VM_PROT_WRITE | VM_PROT_EXECUTE)`).
   - `DarwinVmSpace` entries verify that `__TEXT` is mapped as `RX` (read + execute, strictly non-writable), `__DATA` is mapped as `RW` (read + write, strictly non-executable), and `__LINKEDIT` is mapped as read-only.
   - Hostile binaries with modified `initprot = 0x7` (RWX) are decisively rejected at both static inspection and runtime load phases.

2. **PAGEZERO 4 GB Null Pointer Guard Band**:
   - The first 4 GB virtual address range (`0x00000000` to `0xFFFFFFFF`) is reserved with `vmsize = 0x100000000ULL` and `initprot = 0`.
   - All dereferences (Read, Write, Execute) in this band are intercepted and denied by `DarwinVmSpace::check_access`.
   - PAGEZERO is explicitly excluded from ASLR sliding (`is_pagezero ? 0ULL : slide`).

3. **ASLR Slide Randomization & Distance Invariance**:
   - When ASLR is enabled, all loadable non-PAGEZERO segments (`__TEXT`, `__DATA`, `__LINKEDIT`) are translated by the configured slide offset.
   - The entry point is updated consistently (`entry_point += slide`).
   - The relative address offset between segments (`__DATA.vmaddr - __TEXT.vmaddr`) remains strictly invariant under ASLR slide application.

4. **16 KB Page Granule Alignment**:
   - iPhone 15 Apple Silicon architecture requires 16 KB (`0x4000`) page granules.
   - Segments with virtual addresses misaligned to 16 KB are flagged and rejected by `AppInspector::inspect_binary`.

## Test Results

- **Test Suite**: `stage235_19_macho_wx_tests`
- **Execution Times**:
  - `RelWithDebInfo`: Passed (0.05s)
  - `Debug`: Passed (0.05s)
- **Scenarios**:
  1. `test_multisegment_wx_and_pagezero_guard`: Valid multi-segment W^X conformance & PAGEZERO guard.
  2. `test_aslr_slide_randomization`: ASLR slide application, segment translation, and distance invariance.
  3. `test_hostile_wx_violation_rejection`: Rejection of mutated RWX segments.
  4. `test_misaligned_segment_rejection`: Rejection of 16 KB-misaligned segment headers.
