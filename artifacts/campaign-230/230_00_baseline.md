# WinIOS Campaign 230 — Stage 230.0 Baseline Verification

## Environment & Repository Identity
- **Repository**: `D:\WinISO26\WinIOS\`
- **Branch**: `campaign-191r`
- **HEAD Commit**: `3d1255a019f86bdc7c59d1f2702f69aef9a62ef7` (Campaign 229 Closeout: `3d1255a`)
- **Host**: Windows 11 x64
- **Host Toolchain**: MSVC C++20 (`/W4 /WX /permissive-`), CMake 3.31.6
- **Guest Toolchain**: Zig 0.17.0 (target: `aarch64-freestanding`)
- **Research Profile**: iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`), 16 KB page granule
- **Worktree**: Clean (`working tree clean`, zero uncommitted changes)

## Protected Invariants Verification
| Invariant Target | Expected | Observed | Status |
| :--- | :--- | :--- | :--- |
| `D:\WinISO26\WinISO26_README.md` | Nonexistent | Nonexistent | **PASS** |
| `D:\WinISO26\WinIOS_README.md` | SHA-256 `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | SHA-256 `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | **PASS** |
| `D:\WinISO26\WinIOS\docs\progress.md` | SHA-256 `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | SHA-256 `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | **PASS** |
| `D:\WinISO26\Inferno-Research\` | Untouched directory | Untouched directory | **PASS** |

## Baseline CTest Inventory
- **Registered Tests**: 891
- **Active Regression Inventory**: Tests 812–891 (Campaigns 223–229)
- **Baseline Verification Run (871..891)**: 21/21 passed (100%), 0 failures, 0 skips.

## Pre-existing Limitations & Constraints
- Freestanding EL0 ARM64 emulation on Windows host; no genuine physical Apple silicon or production iOS/Darwin components.
- Cooperative task scheduling; no preemptive multi-core hardware threading claimed.
- Packaging format in Campaign 224 (`WIOSPKG2`) was rudimentary and unversioned with respect to SDK/ABI requirements.
- PID allocation across previous sample workloads used fixed hardcoded identifiers (e.g. 97/98 in Campaign 228, 95/96 in Campaign 229).
