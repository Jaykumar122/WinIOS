# Stage 240.2: Mach-O Loader Conformance

## 1. Conformance Specification
The WinIOS Mach-O loader (`src/loader/macho_loader.cpp`) was evaluated against standard 64-bit ARM64 Mach-O binaries:

- **Supported File Types**: `MH_EXECUTE` (`0x02`).
- **Required Load Commands**:
  - `LC_SEGMENT_64` (`0x19`): Maps segments (`__PAGEZERO`, `__TEXT`, `__DATA`, `__LINKEDIT`).
  - `LC_MAIN` (`0x80000028`) or `LC_UNIXTHREAD` (`0x5`): Establishes entry-point offset.
- **Granularity & Alignment**:
  - Requires 16 KB (`0x4000`) alignment for all loadable segments.
  - Page zero size set to 64 KB (`0x10000`) or 16 KB (`0x4000`) to catch null pointer dereferences.
- **W^X Enforcement**:
  - `__TEXT` mapped with `VM_PROT_READ | VM_PROT_EXECUTE`.
  - `__DATA` mapped with `VM_PROT_READ | VM_PROT_WRITE`.
  - Simultaneous Write and Execute permissions are rejected unconditionally.
- **Malformed Binary Rejection**:
  - Truncated headers return `MachoLoadStatus::kInvalidHeader`.
  - Mismatched CPU types (e.g. x86_64, ARMv7) return `MachoLoadStatus::kUnsupportedArchitecture`.
  - Out-of-bounds segment offsets return `MachoLoadStatus::kMalformedSegments`.

## 2. Test Verification
Verified in `tests/unit/stage240_2_macho_loader_conformance_tests.cpp`:
- Valid Mach-O parsing: PASS.
- W^X violation rejection: PASS.
- Out-of-bounds segment rejection: PASS.
- Truncated header rejection: PASS.
- Missing entrypoint rejection: PASS.
