# WinIOS Campaign 210: Baseline & Safety Audit Report

**Campaign:** 210 — Genuine XNU Kernel Initialization, Thread Scheduling & Darwin Kernel Services  
**Stage:** 210.0 — Repository Baseline and Safety Audit  
**Date:** 2026-11-17  
**Branch:** `campaign-191r`  
**Host:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0, CMake 4.4.4, CTest 4.4.4  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), 6 cores (2 Everest + 4 Sawtooth), 16KB translation granule  

---

## 1. Repository State & Baseline Commit

The repository was verified prior to any modifications:
- **Active Branch:** `campaign-191r`
- **HEAD Commit:** `a41d5be09f00d292c5fdd3cdc323de53d380748c` (WinIOS Campaign 209: Genuine XNU VM Initialization and Physical Page Management)
- **Parent Commit:** `89e415dda8ff1a0faaac253cedf9105abbe681d7` (Campaign 208 Closeout)
- **Working Tree:** Clean, 0 untracked files prior to Campaign 210 directory scaffolding.

---

## 2. Protected Files Integrity Baseline

The following protected assets were audited and their baseline SHA-256 hashes recorded:

| Protected File | Baseline SHA-256 | Verification Status |
| :--- | :--- | :--- |
| `WinIOS_README.md` (`D:/WinISO26/WinIOS_README.md`) | `61f4d078d5071b5ab16093eb5418ed15afb7ed41cd3c84f080ce44e196ea7fef` | Verified / Immutable |
| `docs/progress.md` (`D:/WinISO26/WinIOS/docs/progress.md`) | `447e7b5ab74b9331cbe618bea6561aba83c4d291dc5ccc16df1d72023c462212` | Verified / Immutable |
| `Inferno-Research/` (`D:/WinISO26/Inferno-Research`) | Clean working tree | Verified / Immutable |

---

## 3. Campaign 209 Artifact Verification

The Campaign 209 binary was audited against its recorded closeout hash:
- **Binary Path:** `artifacts/campaign-209/xnu_genuine_init.macho`
- **File Size:** 67,976 bytes
- **SHA-256:** `cc2ad408e64a8bd191b7cc697e1ec6d52d8e505344419b2f59d8ca47cba8f516` (Exact match)
- **Report Path:** `artifacts/campaign-209/WINIOS_CAMPAIGN_209_FINAL_REPORT.md`
- **Manifest:** `artifacts/campaign-209/209_sha256_manifest.txt`

---

## 4. Test Suite Baseline Execution

CTest execution across the Campaign 209 test targets confirmed 100% pass rate:
- **Campaign 209 Tests:** 17 registered, 17 passed, 0 failed (100% pass rate, 1.03s total time).
- **Full Repository Suite:** 617 registered, 616 passed, 1 skipped (`inferno_integration_tests`), 0 failed.

---

## 5. Directory Scaffolding

Created:
- `docs/campaign210/`
- `artifacts/campaign-210/`
- Updated `.gitignore` to whitelist Campaign 210 documentation and artifacts.
