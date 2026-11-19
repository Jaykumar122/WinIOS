# Stage 232.0: Baseline and Campaign 231 Integrity Verification

## 1. Repository and Commit Baseline
- **Repository Path**: `D:\WinISO26\WinIOS\`
- **Branch**: `campaign-191r`
- **Current HEAD Commit**: `076fbac867014a671989d754ad129781388ebf97`
- **Short Hash**: `076fbac`
- **Commit Message**: `feat(campaign-231): package authenticity, transactional runtime, ABI conformance and reproducible tooling`
- **Working Tree State**: Pristine, clean, 0 untracked files, 0 unstaged modifications.

## 2. Protected Invariant Verification (Bit-for-Bit)

| Invariant | Path | Verification Target | Observed Result | Status |
|---|---|---|---|---|
| **Invariant 1** | `D:\WinISO26\WinISO26_README.md` | Nonexistent (`False`) | `False` | **PASS** |
| **Invariant 2** | `D:\WinISO26\WinIOS_README.md` | SHA-256 `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | **PASS** |
| **Invariant 3** | `D:\WinISO26\WinIOS\docs\progress.md` | SHA-256 `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | **PASS** |
| **Invariant 4** | `D:\WinISO26\Inferno-Research\` | Untouched directory (112 items) | 112 items | **PASS** |

## 3. Toolchain and Build Configuration
- **Host OS**: Windows 11 Enterprise x64 (Build 26100)
- **Compiler**: Microsoft Visual C++ 2022 (MSVC C++20 `/W4 /WX /permissive-`)
- **Build System**: CMake 3.31.6 / MSBuild 18.10.1
- **Guest Toolchain**: Zig 0.17.0 (`aarch64-freestanding`)
- **Guest Profile**: iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`), 16 KB virtual memory page granule

## 4. Test Inventory and Baseline Measurements

### Total CTest Inventory: 924 Registered Tests
- **Total Registered**: 924
- **Active Regression Range (Tests #871..#924)**: 54 test suites
  - `RelWithDebInfo`: **54/54 Passed (100%)** in 1.95s
  - `Debug`: **54/54 Passed (100%)** in 2.38s
- **Full Repository Test Suite (Tests #1..#924)**:
  - **Executed**: 923
  - **Skipped**: 1 (`#383 inferno_integration_tests`)
  - **Passed**: 901 (97.6%)
  - **Failed**: 22 (historical tests from early campaigns #211..#227 incompatible with later architecture)

## 5. Compiler and Linker Warnings
All core libraries (`winios_core.lib`) and active test executables compile under `/W4 /WX /permissive-` with **0 compiler warnings and 0 linker warnings**.
