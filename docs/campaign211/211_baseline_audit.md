# WINIOS CAMPAIGN 211: STAGE 211.0 BASELINE AUDIT

**Campaign:** 211  
**Stage:** 211.0 — Baseline and Repository Safety Audit  
**Date:** 2026-11-17  
**Active Branch:** `campaign-191r`  
**Starting Commit:** `7ac82f9b6a02439db58d2bdf5d20d72023bb64dd`  
**Toolchain:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0, CMake 4.4.4, CTest 4.4.4  

---

## 1. Git Repository State

- **Branch Check:** Confirmed on branch `campaign-191r`.
- **Commit Check:** HEAD commit is `7ac82f9b6a02439db58d2bdf5d20d72023bb64dd`:
  `WinIOS Campaign 210: Genuine XNU Kernel Initialization, Thread Scheduling and Darwin Services`
- **Working Tree:** Prior to directory and gitignore additions for Campaign 211, working tree was completely clean.

---

## 2. Protected Files Cryptographic Verification

The protected paths have been audited and verified:

| Protected Path | SHA-256 Digest | Status |
|:---|:---|:---:|
| `D:\WinISO26\WinIOS_README.md` | `61f4d078d5071b5ab16093eb5418ed15afb7ed41cd3c84f080ce44e196ea7fef` | Verified Bit-Exact |
| `D:\WinISO26\WinIOS\docs\progress.md` | `447e7b5ab74b9331cbe618bea6561aba83c4d291dc5ccc16df1d72023c462212` | Verified Bit-Exact |
| `D:\WinISO26\Inferno-Research/` | Recursive trees inspected | Untouched / Immutable |

---

## 3. Campaign 210 Deliverable Verification

Verified all Campaign 210 deliverables:
- `artifacts/campaign-210/xnu_genuine_init.macho`: `f6e159b9a271d8b6f6a81472ab94b283f26fd711e584b145b28af974240d65e3`
- `docs/campaign210/WINIOS_CAMPAIGN_210_FINAL_REPORT.md`: `38a998e82c3d14142c31ae4c75ba749a92af691e1882bae69ad2b13ce0e4c73b`
- `docs/campaign210/210_sha256_manifest.txt`: Validated

---

## 4. Baseline Regression Suite Run

Execution of CTest in Release configuration across all registered project test suites:
- **Total Test Suites Registered:** 637
- **Total Test Suites Executed:** 636
- **Total Passed:** 636 (100% of executed suites)
- **Total Skipped:** 1 (`inferno_integration_tests` due to standalone harness requirements)
- **Total Failed:** 0
- **Pass Rate:** 100.0%

---

## 5. Workspace Initialization

- Created directory `docs/campaign211/`.
- Created directory `artifacts/campaign-211/`.
- Whitelisted `docs/campaign211/` and `artifacts/campaign-211/` in `.gitignore`.
- Stage 211.0 completed successfully.
