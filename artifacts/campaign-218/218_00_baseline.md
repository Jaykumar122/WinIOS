# WINIOS CAMPAIGN 218 — STAGE 218.0 BASELINE INTEGRITY & REPRODUCIBILITY AUDIT

**Date:** November 18, 2026  
**Campaign:** 218 (Stage 218.0)  
**Host Environment:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`), CMake 3.31.6, CTest  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), 16 KB Granule  
**Branch:** `campaign-191r`  
**Baseline Git Commit:** `831ee549980a0f219fefe0edaefcd7cf0532d47b` (Campaign 217 closeout)

---

## 1. Repository & Worktree Integrity

- **Repository Root:** `D:\WinISO26\WinIOS\`
- **Current Branch:** `campaign-191r`
- **Current HEAD Commit:** `831ee549980a0f219fefe0edaefcd7cf0532d47b`
- **Worktree State:** Clean (`git status --short` returned 0 modifications)

---

## 2. Bit-for-Bit Protected Paths Verification

| Protected Path | Expected Status / Hash | Observed Status / Hash | Verification Result |
|---|---|---|---|
| `D:\WinISO26\WinISO26_README.md` | Must NOT exist (`False`) | Non-existent (`False`) | MATCH / PASS |
| `D:\WinISO26\WinIOS_README.md` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | MATCH / PASS |
| `D:\WinISO26\WinIOS\docs\progress.md` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | MATCH / PASS |
| `D:\WinISO26\Inferno-Research\` | Pristine & untouched | Unmodified directory structure verified | MATCH / PASS |

---

## 3. Initial Test Inventory Audit

- **Registered Test Suites:** 744 registered CTest test suites (`#1` through `#744`).
- **Baseline Test Suite Coverage:**
  - Campaign 215: 12 test suites (`#712` – `#723`)
  - Campaign 216: 14 test suites (`#724` – `#737`)
  - Campaign 217: 7 test suites (`#738` – `#744`)
- **Compilers Available:**
  - Host MSVC: Visual Studio 2026 C++20 compiler (`cl.exe 19.50+`)
  - Host CMake: 3.31.6
  - Cross Compiler: Windows Zig 0.17.0 (`zig cc -target aarch64-macos`)
