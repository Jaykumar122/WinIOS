# Stage 235.0 — Baseline and Repository Safety

**Date:** 2026-11-20  
**Campaign:** 235.0  
**Subsystem:** Repository Safety / Baseline Verification  
**Status:** Complete  

---

## 1. Executive Summary

Stage 235.0 verifies the repository baseline and safety invariants prior to initiating Campaign 235 engineering stages.
The repository is hosted on Windows 11 x64 targeting a research model of freestanding ARM64 guest applications (iPhone 15, `iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`, 16 KB page granule, freestanding Mach-O EL0).

---

## 2. Git Lineage and Baseline Identification

- **Current Branch:** `campaign-191r`
- **Current Full HEAD Commit:** `789ac7bccfec6f0277b30d885350f6f8f4e18ee5`
- **Commit Subject:** `feat(campaign-234): security assurance closure, handle-based filesystem confinement, WAL durability & cryptographic validation`
- **Immediate Parent Commit:** `655b43d8137a0d59d1cf4867a98715a7109deddc` (Campaign 233 closeout)
- **Lineage Verification:** Clean single-parent progression from Campaign 230 through 234:
  - `789ac7bccfec6f0277b30d885350f6f8f4e18ee5` (Campaign 234 closeout)
  - `655b43d8137a0d59d1cf4867a98715a7109deddc` (Campaign 233 closeout)
  - `429e3a8e1f52a664d0900fc9a9170693cc7ab7a0` (Campaign 232 closeout)
  - `076fbac867014a671989d754ad129781388ebf97` (Campaign 231 closeout)
  - `8d8b21842777afada6ea481a0db6fc2e2b344f81` (Campaign 230 closeout)

---

## 3. Working Tree and Protected Invariants

- **Working Tree Status:** Clean (`nothing to commit, working tree clean`).
- **Protected Invariants:**
  1. `D:\WinISO26\WinISO26_README.md`: **Nonexistent** (Verified).
  2. `D:\WinISO26\WinIOS_README.md`: SHA-256 = `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` (Verified bit-for-bit).
  3. `D:\WinISO26\WinIOS\docs\progress.md`: SHA-256 = `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` (Verified bit-for-bit).
  4. `D:\WinISO26\Inferno-Research\`: Untouched (Verified).
  5. Strict Compiler Flags: MSVC C++20 `/W4 /WX /permissive-` (0 warnings, 0 errors).

---

## 4. Test Inventory Baseline

- **Build Directory:** `D:\WinISO26\WinIOS\build`
- **Build Configurations:** `RelWithDebInfo`, `Debug`
- **Total Registered CTest Targets:** 973 tests
  - Baseline CTest targets (#1 through #940)
  - Campaign 233 verification suite (#941 through #955)
  - Campaign 234 assurance suite (#956 through #973)
- **Repository Safety Assessment:** Pristine, verified, ready for sequential execution.
