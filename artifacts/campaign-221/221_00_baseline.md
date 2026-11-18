# WinIOS Campaign 221 — Stage 221.0 Baseline Report

**Date:** 2026-11-18  
**Campaign:** 221 — Native ARM64 Application Runtime, Guest SDK v1.5, C Runtime, Persistent Storage & Application Lifecycle  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Starting Baseline Commit:** `415d254b3321d8b2e67d90915c00aabc1ae36dd4`  
**Toolchain:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`), CMake 3.31.6, CTest, Zig 0.17.0  

---

## 1. Baseline Git & Repository State

- **Branch:** `campaign-191r`
- **HEAD Commit:** `415d254b3321d8b2e67d90915c00aabc1ae36dd4`
- **Status:** Clean working tree. No unstaged or untracked changes.
- **Git History:** 33 commits ahead of origin/campaign-191r (all local commits preserved, strictly no remote pushes).

---

## 2. Non-Negotiable Protected Path Verification

| Protected Path | Expected State / Hash | Observed State / Hash | Verification |
|---|---|---|---|
| `D:\WinISO26\WinIOS_README.md` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | **PASS (100% Match)** |
| `D:\WinISO26\WinIOS\docs\progress.md` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | **PASS (100% Match)** |
| `D:\WinISO26\WinISO26_README.md` | MUST NOT EXIST | Absent (`Test-Path` returned False) | **PASS** |
| `D:\WinISO26\Inferno-Research\` | Unmodified research directory | Present and untouched | **PASS** |

*(Note on prompt hash: The prompt noted `DC3C84` vs `CD3C84` in `WinIOS_README.md`. The actual on-disk hash is unchanged since Campaigns 215–220: `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF`.)*

---

## 3. Subsystem Inspection & Architecture Map

- **Kernel & Process Subsystems:**
  - `src/kernel/bsd_process.cpp / .h`: Multi-process container with PID, PPID, independent `UserAddressSpace` pointer, ASID (1..255), file descriptor table (`std::array<std::shared_ptr<FileDescriptor>, 64>`), process state machine, exit status.
  - `src/kernel/thread.cpp / .h`: `KernelThread` with ARM64 CPU context (X0–X30, PC, SP_EL0, PSTATE, TPIDR_EL0, FPCR, FPSR).
  - `src/kernel/multi_process_scheduler.cpp / .h`: Preemptive/round-robin scheduler dispatching runnable threads across processes, managing ASID switching and page table base updates.
  - `src/kernel/user_syscall_dispatcher.cpp / .h`: Dispatches Category A (BSD syscalls), Category B (Mach-message inspired traps), and Category C (WinIOS-accelerated syscalls).
- **Memory & Translation Subsystems:**
  - `src/memory/user_address_space.cpp / .h`: 16 KB page granule translation, Stage 1 MMU emulation, ASID ownership, memory protection bits (R/W/X).
  - `src/memory/virtual_page_table.cpp / .h`: Multi-level page tables with TLB tracking and ASID tagging.
- **Packaging & Storage Subsystems:**
  - `src/packaging/app_package.cpp / .h`: Package container (`.wios`), manifest serialization/deserialization.
  - `src/packaging/package_installer.cpp / .h`: Package verification, installation to `/apps/<app_id>/`.
  - `src/packaging/app_registry.cpp / .h`: Registry of installed applications.
  - `src/storage/isolated_storage.cpp / .h`: Per-app isolated storage root (`/data/<app_id>/`) with quota limits and path validation.
- **UI & Presentation Subsystems:**
  - `src/ui/display_window.cpp / .h`: Host desktop display window and Win32 presentation.
  - `src/input/host_input_queue.cpp / .h`: Normalization of keyboard, mouse, and touch events into `winios_event_t`.

---

## 4. Test Suite Baseline

- **Total Registered CTest Targets:** 786 tests.
- **Campaign 220 Test Range:** Tests #767 through #786 (20 suites, 100% passing).
- **Build Configurations Verified:**
  - `Release`: `winios_core` built cleanly with zero warnings, zero errors.
  - `Debug`: `winios_core` built cleanly with zero warnings, zero errors.

---

## 5. Acceptance Status

Stage 221.0 acceptance criteria are fully met: exact baseline, repository state, protected file integrity, toolchains, and initial subsystem architecture are verified and recorded.
