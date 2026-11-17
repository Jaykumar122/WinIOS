# WINIOS CAMPAIGN 211 — GIT AUDIT & INTEGRITY VERIFICATION

**Date:** 2026-11-17  
**Campaign:** 211  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / A16 Bionic `T8120`)  
**Host Toolchain:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0  

---

## 1. Branch and Baseline Verification

- **Active Branch:** `campaign-191r` (Verified: exact match)
- **Starting Baseline Commit:** `7ac82f9b6a02439db58d2bdf5d20d72023bb64dd` (Campaign 210 closeout)
- **Local Commit Policy:** Local commits only on `campaign-191r` — **DO NOT PUSH**.

---

## 2. Protected Files Integrity Audit

The following protected files and directories were audited before campaign commencement and re-verified at campaign closeout. All computed SHA-256 hashes are bit-exact:

| Protected Asset | Expected SHA-256 Hash | Post-Campaign Computed SHA-256 Hash | Integrity Status |
|:---|:---|:---|:---:|
| `D:\WinISO26\WinIOS_README.md` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | **UNMODIFIED** |
| `D:\WinISO26\WinIOS\docs\progress.md` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | **UNMODIFIED** |
| `D:\WinISO26\Inferno-Research/` | *Protected tree* | *Protected tree* | **UNTOUCHED** |

---

## 3. Campaign 211 File Modifications & Additions

### Added Kernel & Runtime Sources:
- `src/kernel/arm_trap_frame.h` & `src/kernel/arm_trap_frame.cpp` (304-byte ARM64 trap frame and routines)
- `src/kernel/bsd_process.h` & `src/kernel/bsd_process.cpp` (BSD process abstraction, lifecycle, file descriptors, IPC space)
- `src/memory/user_address_space.h` & `src/memory/user_address_space.cpp` (TTBR0 user address space, Stage-1 tables, copyin/copyout)
- `src/loader/macho_user_loader.h` & `src/loader/macho_user_loader.cpp` (64-bit ARM64 Mach-O loader, LC_MAIN, LC_UNIXTHREAD)
- `src/loader/embedded_user_apps_211.inl` (Embedded C++ headers containing compiled ARM64 Mach-O binaries)
- `src/kernel/user_stack.h` & `src/kernel/user_stack.cpp` (AAPCS64 user stack builder)
- `src/kernel/user_syscall_dispatcher.h` & `src/kernel/user_syscall_dispatcher.cpp` (Darwin & Fast ABI syscall dispatcher and VirtualConsole)
- `src/runtime/userspace_coordinator.h` & `src/runtime/userspace_coordinator.cpp` (End-to-end userspace execution coordinator)

### Added Test Suites:
- `tests/unit/stage211_2_context_preservation_tests.cpp`
- `tests/unit/stage211_3_el0_privilege_tests.cpp`
- `tests/unit/stage211_5_bsd_process_tests.cpp`
- `tests/unit/stage211_7_user_address_space_tests.cpp`
- `tests/unit/stage211_9_macho_user_loader_tests.cpp`
- `tests/unit/stage211_11_user_stack_tests.cpp`
- `tests/unit/stage211_13_first_el0_execution_tests.cpp`
- `tests/unit/stage211_15_essential_syscalls_tests.cpp`
- `tests/unit/stage211_17_mach_ipc_expansion_tests.cpp`
- `tests/unit/stage211_20_minimal_init_process_tests.cpp`
- `tests/unit/stage211_21_first_userspace_integration_tests.cpp`
- `tests/unit/stage211_22_adversarial_userspace_security_tests.cpp`
- `tests/unit/stage211_23_snapshot_replay_tests.cpp`

### Generated Binaries & Documentation:
- `artifacts/campaign-211/user_first_app.macho`
- `artifacts/campaign-211/user_init_app.macho`
- `docs/campaign211/` deliverables and reports.
