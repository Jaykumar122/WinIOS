# WinIOS Campaign 216 — Baseline Integrity and Repository Audit

**Campaign:** 216  
**Stage:** 216.0 (Baseline Integrity and Repository Audit)  
**Date:** 2026-11-18  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Target Research Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / `T8120`), 16 KB translation granule  
**Host Platform:** Windows 11 x64, MSVC C++20 (`/W4 /WX /permissive-`), CMake 3.31.6  

---

## 1. Baseline Git State & Commit Verification

- **HEAD Commit SHA:** `c5509421bb723b1910281d16134f846962141b13`
- **Commit Message:** `WinIOS Campaign 215: Application platform hardening, packages, isolated storage & developer SDK`
- **Working Tree State:** Completely clean (`nothing to commit, working tree clean`)
- **Required Baseline Match:** **CONFIRMED** — HEAD exactly equals required baseline `c5509421bb723b1910281d16134f846962141b13`.

---

## 2. Protected Paths Integrity Audit

All 4 non-negotiable protected paths were audited and verified:

| Protected Path | Expected State / Hash | Observed State / Hash | Status |
| :--- | :--- | :--- | :---: |
| `D:\WinISO26\WinISO26_README.md` | Non-existent (`False`) | Non-existent (`False`) | **VERIFIED** |
| `D:\WinISO26\WinIOS_README.md` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | **VERIFIED** |
| `D:\WinISO26\WinIOS\docs\progress.md` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | **VERIFIED** |
| `D:\WinISO26\Inferno-Research\` | Untouched directory | Directory exists, untracked by WinIOS git | **VERIFIED** |

---

## 3. Subsystem Architecture & Baseline Inventory

### 3.1 Kernel & Category C ABI
- Syscalls 500..507 are implemented as Category C extensions in `user_syscall_dispatcher.cpp`:
  - 500: `kSysWiniosWindowCreate`
  - 501: `kSysWiniosWindowDestroy`
  - 502: `kSysWiniosSurfaceUpdate`
  - 503: `kSysWiniosSurfaceCopy`
  - 504: `kSysWiniosPollEvent`
  - 505: `kSysWiniosSurfaceFill`
  - 506: `kSysWiniosSetWindowTitle`
  - 507: `kSysWiniosGetAbiVersion` (returns `0x00010000`)
- Dual ABI conventions supported: DarwinStrict (`X16` number, `X0..X5` args, `PSTATE.C` flag) and WinIOSFast (`X0` number, `X1..X6` args, negative errno).
- `GuestMemoryValidator` (`src/memory/guest_memory_validator.h/.cpp`) performs 8-point security check on all guest pointers.

### 3.2 Packaging, Registry & Storage
- `.wios` format container v1.0 with SHA-256 file manifest verification.
- Transactional `PackageInstaller` with staging in `/staging/<bundle_id>/`, atomic commit to `/Applications/<bundle_id>.app/`, and rollback on corruption.
- `IsolatedStorage` in `/data/<bundle_id>/` with path traversal defense and crash recovery.
- `CapabilityManager` and `SystemClipboard` with caller audit logging.

### 3.3 Display & Input
- `DisplayWindow` (`src/ui/display_window.h/.cpp`): provides a Win32 desktop presentation surface or headless virtual backbuffer.
- `WindowManager` (`src/graphics/window_manager.h/.cpp`): tracks windows, but the compositor is currently tailored for single top-level presentation.
- `HostInputQueue`: bounded ring buffer (capacity 256) with drop policies.

---

## 4. Existing Architectural Limitations Identified

1. **Single-Window Presentation Focus**: While `WindowManager` can store multiple `Window` objects, `DisplayWindow` only presents a single flat framebuffer. There is no multi-window desktop compositor that renders multiple overlapping windows with z-ordering, window decorations (title bar, close/minimize buttons), window dragging, resizing, and partial damage tracking.
2. **Input Provenance Ambiguity**: Current event logging records timestamps and basic coordinates, but lacks explicit categorization of event provenance (`PhysicalWindowsInput` vs `WindowsMessageInjected` vs `TestInjected` vs `GuestGenerated` vs `SyntheticHostEvent`).
3. **Absence of Real Mouse & Keyboard Win32 Event Pipeline**: Win32 window message handling in `DisplayWndProc` only handled basic keystrokes and single mouse clicks. Mouse movement tracking, wheel scrolling, middle/right button clicks, mouse capture/release, and window hit-testing for decorations were not wired.
4. **No Renderer Abstraction & CPU-Only Rasterization**: All rendering is CPU-based software pixel manipulation into an ARGB buffer. There is no pluggable `Renderer` interface separating composition from presentation, no optional hardware presentation (such as Direct3D 11), and no device loss/recovery mechanisms.
5. **Sample Applications are Host Models**: `CalculatorApp` and `TextEditorApp` run as C++ classes executing on the host rather than freestanding ARM64 Mach-O binaries executing EL0 guest instructions.
6. **No Multi-Window Guest ABI**: Guest applications have no syscalls to query window geometry, request window move/resize, manage z-order/focus, or receive window lifecycle notifications.

---

## 5. Conclusion & Baseline Gate Status

Stage 216.0 acceptance criteria are fully met. The baseline commit is confirmed, repository state is verified, protected paths are untouched, and architectural gaps for Campaign 216 have been systematically catalogued.
