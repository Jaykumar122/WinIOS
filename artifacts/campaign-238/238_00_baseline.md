# Stage 238.0: Baseline and Repository Protection Verification

## Overview
Stage 238.0 establishes the verified baseline for WinIOS Campaign 238. It performs strict validation of Git HEAD, branch, commit ancestry, working-tree status, protected repository invariants, and the registered CTest inventory prior to making any modifications.

## Baseline Verification Details
- **Timestamp**: 2026-11-20T18:58:00+05:30
- **Platform**: Windows 11 x64
- **Host Compiler**: MSVC C++20 (`/W4 /WX /permissive-`)
- **Build System**: CMake 3.31.6
- **Guest Toolchain**: Zig 0.17.0
- **Target Research Profile**: iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`), ARM64 EL0 freestanding Mach-O, 16 KB granule

## Git Repository State
- **Repository Directory**: `D:\WinISO26\WinIOS\`
- **Branch**: `campaign-191r`
- **Current HEAD Commit**: `1b81b44a887b214b93a357c2351cb0af8ddc8dc7`
- **Expected Baseline Commit**: `1b81b44a887b214b93a357c2351cb0af8ddc8dc7`
- **Ancestry Verification**:
  - `1b81b44a887b214b93a357c2351cb0af8ddc8dc7` (Campaign 237 closeout)
  - `3befe1bcd2997b03bb4af5568e5e2b57cda747be` (Campaign 236 closeout)
  - `f81467bcc800970bc8391c6d7eac0fb419bb5524` (Campaign 235 closeout)
- **Working Tree Cleanliness**: Completely clean (`git status -s` produced 0 lines).

## Protected Repository Invariants
1. `D:\WinISO26\WinISO26_README.md`: Nonexistent (`Test-Path` == `False`).
2. `D:\WinISO26\WinIOS_README.md`: SHA-256 = `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` (Verified bit-for-bit identical).
3. `D:\WinISO26\WinIOS\docs\progress.md`: SHA-256 = `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` (Verified bit-for-bit identical).
4. `D:\WinISO26\Inferno-Research\`: Completely untouched, uninspected, and isolated.

## Initial Registered CTest Inventory
- **Total Registered Test Suites**: 988 (`ctest -N` reports `Total Tests: 988`).
- **Configuration Folders**: `D:\WinISO26\WinIOS\build\` (`RelWithDebInfo` and `Debug` targets).
- **External Integration Skip**: Test `#383: inferno_integration_tests` is intentionally skipped by policy.
