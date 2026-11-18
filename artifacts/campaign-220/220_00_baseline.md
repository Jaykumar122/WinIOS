# WinIOS Campaign 220 — Stage 220.0 Baseline Integrity & Repository Audit

## 1. Executive Baseline Summary

- **Campaign:** 220 (Stages 220.0–220.24)
- **Host Target:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`), CMake 3.31.6, CTest
- **Cross-Compiler:** Zig 0.17.0 (`zig cc -target aarch64-macos`)
- **Guest Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), ARM64, 16 KB translation granule
- **Repository Root:** `D:\WinISO26\WinIOS\`
- **Branch:** `campaign-191r`
- **Required Baseline Commit:** `8f00af545488e9641070b4ac2bdeb7c6374aca75`
- **Current HEAD Commit:** `8f00af545488e9641070b4ac2bdeb7c6374aca75` (**VERIFIED MATCH**)
- **Working Tree State:** Completely clean (`nothing to commit, working tree clean`)

---

## 2. Protected Paths Integrity Audit

All 4 non-negotiable protected paths were audited and verified before any modifications:

| Protected Path | Constraint | Verification Result | Status |
| :--- | :--- | :--- | :--- |
| `D:\WinISO26\WinISO26_README.md` | Must NOT exist | Does Not Exist (`Test-Path: False`) | **PASS** |
| `D:\WinISO26\WinIOS_README.md` | SHA-256 = `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | Matches `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | **PASS** |
| `D:\WinISO26\WinIOS\docs\progress.md` | SHA-256 = `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | Matches `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | **PASS** |
| `D:\WinISO26\Inferno-Research\` | Must not be modified | Directory present, untouched (`LastWriteTime: 11/3/2026 11:14 AM`) | **PASS** |

---

## 3. Campaign 219 Closeout Change Set Inspection

The baseline commit `8f00af545488e9641070b4ac2bdeb7c6374aca75` introduced 63 files, 13,521 insertions, and 5 deletions:
- **Build & SDK:** `CMakeLists.txt`, `include/winios_sdk/winios_sdk.h`
- **Kernel & Architecture:** `src/kernel/bsd_process.h`, `src/kernel/bsd_process.cpp`, `src/kernel/thread.h`, `src/kernel/thread.cpp`, `src/kernel/multi_process_scheduler.cpp`, `src/kernel/user_syscall_dispatcher.h`, `src/kernel/user_syscall_dispatcher.cpp`
- **Services & Loader:** `src/services/service_manager.h`, `src/services/service_manager.cpp`, `src/loader/macho_user_loader.h`, `src/loader/macho_user_loader.cpp`, `src/loader/embedded_campaign219_apps.inl`
- **Applications:** `src/apps/campaign219_supervisor.c`, `src/apps/campaign219_worker.c`, `src/apps/campaign219_threaded_math.c`
- **Unit Tests:** 13 new CTest suites (`stage219_3`, `stage219_5`, `stage219_8`, `stage219_10`, `stage219_11`, `stage219_13`, `stage219_14`, `stage219_15`, `stage219_17`, `stage219_18`, `stage219_20`, `stage219_22`, `stage219_23`)
- **Artifacts:** 34 files in `artifacts/campaign-219/` including 4 BMP visual captures and manifest.

---

## 4. CTest Registration & Test Inventory

Independent verification of CTest in `D:\WinISO26\WinIOS\build`:
- **Total RegisteredTests:** 766
- **Test ID Range:** Test #1 (`memory_tests`) through Test #766 (`stage219_23_adversarial_tests`).
- **Recent Campaign Distribution:**
  - Campaign 215: Tests #712–#723 (12 suites)
  - Campaign 216: Tests #724–#737 (14 suites)
  - Campaign 217: Tests #738–#744 (7 suites)
  - Campaign 218: Tests #745–#753 (9 suites)
  - Campaign 219: Tests #754–#766 (13 suites)
  - Pre-Campaign 215: Tests #1–#711 (711 suites)
- **Active Regression Status:** All 55 test suites across Campaigns 215–219 pass 100% (55/55 passed, 0 failures).

---

## 5. Build Environment Audit

- Host: Windows 11 Version 10.0.26100 (x64)
- Compiler: Microsoft Visual Studio 2026 MSVC C++20 (`cl.exe` with `/W4 /WX /permissive-`)
- Build Directory: `D:\WinISO26\WinIOS\build` (Multi-config Visual Studio generator)
- Available Configurations: `Release`, `Debug`
- Toolchain Tools: `cmake` 3.31.6, `ctest` 3.31.6, `zig` 0.17.0
