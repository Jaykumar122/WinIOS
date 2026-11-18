# WinIOS Campaign 228 — Stage 228.0 Baseline Verification

## 1. Environment & Integrity State

- **Date:** 2026-11-18
- **Repository:** `D:\WinISO26\WinIOS\`
- **Branch:** `campaign-191r`
- **Required Baseline Commit:** `f879deac05bb76a080b2187a8978c8175b2b632b`
- **Actual HEAD Commit:** `f879deac05bb76a080b2187a8978c8175b2b632b` (Matched)
- **Worktree State:** Clean (`nothing to commit, working tree clean`)
- **Host OS:** Windows 11 x64
- **Host Compiler:** MSVC C++20 (`/W4 /WX /permissive-`)
- **Build System:** CMake 4.4.4 / MSBuild (Visual Studio 2026)
- **Cross-Compiler:** Zig 0.17.0 (`target aarch64-macos -nostdlib -fno-builtin -fno-sanitize=all -O2`)
- **Target Research Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), ARM64 EL0, 16 KB Granule, public pinned `xnu-10002.1.13`

## 2. Invariant Assets Verification

| Invariant Path | Expected State | Actual State | Result |
|---|---|---|---|
| `D:\WinISO26\WinISO26_README.md` | Nonexistent (`False`) | Nonexistent (`False`) | **VERIFIED** |
| `D:\WinISO26\WinIOS_README.md` | SHA-256 `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | Matches Expected | **VERIFIED** |
| `D:\WinISO26\WinIOS\docs\progress.md` | SHA-256 `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | Matches Expected | **VERIFIED** |
| `D:\WinISO26\Inferno-Research\` | Exists & Untouched (`True`) | Exists & Untouched (`True`) | **VERIFIED** |

## 3. Baseline Test Inventory

- **CTest Discovered Targets:** 870 tests.
- **Campaign 227 Test Suite:** 10 / 10 passing (100% pass rate in 0.58s).
- **Existing Syscall Range:** Category C ABI v2.1 allocated Syscalls 501..580.
- **Syscall Slots 581..590:** Reserved for Category C ABI v2.2 (System Settings, Control Center & Accessibility).
