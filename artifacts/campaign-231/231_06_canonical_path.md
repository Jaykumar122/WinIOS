# Campaign 231 — Stage 231.6: Canonical Package Path and Filesystem Boundary Enforcement

## Objective
Implement a robust canonical path validator (`winios::packaging::CanonicalPathValidator`) to secure host/guest filesystem boundaries against path traversal, Windows NTFS reserved device namespace poisoning, Alternate Data Streams (ADS), drive letters, UNC shares, and trailing whitespace/dot normalization bypasses.

## Implementation Details
- **Files Created/Modified:**
  - `src/packaging/canonical_path_validator.h`: Defined `PathValidationStatus` enum, maximum depth (16 levels), maximum length (256 chars), and validation prototypes.
  - `src/packaging/canonical_path_validator.cpp`: Implementation of `validate_and_normalize_bundle_path`, `is_windows_reserved_device_name`, and `is_strictly_inside_root`.
  - `CMakeLists.txt`: Added `canonical_path_validator.cpp` to `winios_core` and registered unit test `stage231_6_canonical_path_tests`.
  - `tests/unit/stage231_6_canonical_path_tests.cpp`: Unit test suite testing all boundary attack vectors.

## Attack Vectors Tested & Defenses Implemented
1. **Separator Normalization:** Mixed `\` and `/` separators normalized to single `/`.
2. **Path Traversal:** Reject `../`, `..\\`, `foo/../../bar` attempting to escape the root.
3. **Reserved Devices:** Reject Windows reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`) regardless of file extension or casing (e.g. `con.txt`, `aux.dat`).
4. **Alternate Data Streams & Drive Letters:** Reject any `:` (e.g. `C:\path`, `file.txt:stream`).
5. **UNC Network Paths:** Reject `//server/share` and `\\server\share`.
6. **Trailing Dots/Spaces:** Reject trailing `.` or space in segment to avoid NTFS truncation vulnerabilities.
7. **Control / Null Characters:** Strict rejection of ASCII `< 0x20` and `\0`.
8. **Depth & Length:** Enforced maximum 16 directory depth and 256 character length.
9. **Root Confinement:** `is_strictly_inside_root` enforces slash-boundary prefix matching.

## Verification & Test Results
- Test Target: `stage231_6_canonical_path_tests` (Test #908)
- Configurations Tested:
  - `RelWithDebInfo`: Passed (0.04 sec)
  - `Debug`: Passed (0.05 sec)
