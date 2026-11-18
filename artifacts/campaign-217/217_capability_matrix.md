# WINIOS CAMPAIGN 217 — CAPABILITY & COMPATIBILITY MATRIX

**Date:** November 18, 2026  
**Campaign:** 217  
**Host:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), ARM64, 16 KB Granule  
**Execution Baseline:** `db6f6827a0d8532a20b5c6c241a1280d7ef6248d` (Campaign 216 closeout)

---

## 1. Runtime & Execution Contract

| Subsystem / Contract | Specification / Standard | Status | Evidence / Notes |
|---|---|---|---|
| Guest ABI Contract | ABI v1.2 (`0x00010200`) | VERIFIED | `GuestAppContract` sets X0=arg_block, SP 16-byte aligned, exit stub |
| Backward Compatibility | ABI v1.1 (`0x00010100`) & v1.0 | VERIFIED | All 14 tests in `stage216_abi_v11_tests` pass 100% |
| Memory Granule | 16 KB page translation granule | VERIFIED | A16 Bionic translation granule preserved across all paging & VM maps |
| Exit Descriptor | `GuestExitDescriptor` (exit_code, reason, retired_insns) | VERIFIED | Clean teardown recorded in `GuestRuntimeLifecycle` |
| Native ARM64 Cross-Compilation | Windows Zig 0.17.0 toolchain (`-target aarch64-macos`) | VERIFIED | Produces valid 64-bit Mach-O (`MH_MAGIC_64`, `CPU_TYPE_ARM64`) |

---

## 2. Developer Framework & Core Services

| Framework Feature | Implementation Class | Status | Evidence / Notes |
|---|---|---|---|
| Memory Allocator | `GuestAllocator` | VERIFIED | Bounded per-process heap, quota enforcement, in-place & bump realloc, leak diagnostics |
| Minimal Freestanding CRT | `mini_crt` | VERIFIED | `w_memcpy`, `w_memset`, `w_strlen`, `w_snprintf`, `w_atoi` freestanding |
| Sandboxed Storage API | `GuestStorageApi` | VERIFIED | High-level API over `IsolatedStorage` with quota limits, path canonicalization |
| Event Loop & Dispatch | `GuestEventLoop` | VERIFIED | Event queue, peek/wait, Win32 input provenance preservation, timer tick dispatch |
| Asynchronous I/O | `GuestAsyncIoManager` | VERIFIED | Bounded submission queue, simulated completion, non-blocking polling |
| Interprocess Communication | `GuestIpcRouter` | VERIFIED | Named ports, bounded mailbox (max 64 msgs), quota limits, auto-cleanup on exit |
| Resource Bundles | `ResourceBundle` | VERIFIED | `.wiosres` archive format with CRC32 integrity, magic `WRES` |
| Resource Asset Loader | `ResourceManager` | VERIFIED | Bundled asset cache, uncompressed BMP (24bpp / 32bpp) decoder |
| Font & Text Engine | `FontEngine` | VERIFIED | 8x16 bitmap font, glyph metrics, string rendering, line wrapping, foreground/background color |
| UI Framework Core | `GuestWidget`, `GuestLabel`, `GuestButton`, `GuestTextInput`, `GuestImageView` | VERIFIED | Widget hierarchy, event propagation, stateful rendering, hit testing |
| Declarative Layout Engine | `GuestLayoutEngine` | VERIFIED | Linear layout (Horizontal/Vertical), alignment, margins, padding, size calculation |
| Application Lifecycle | `AppLifecycleManager` | VERIFIED | States: Uninitialized, Inactive, Active, Backgrounded, Suspended, Terminated |
| Preferences & Config | `AppPreferences` | VERIFIED | Key-value store (string, int, bool, float), persisted to sandboxed storage |

---

## 3. Native ARM64 Guest Application Suite

| Application | Bundle Identifier | Architecture | Capabilities Exercised | Status |
|---|---|---|---|---|
| Runtime Inspector | `com.winios.runtimeinspector` | ARM64 Mach-O | Allocator stats, memory layout, retired instruction count, runtime state | VERIFIED |
| Resource Viewer | `com.winios.resourceviewer` | ARM64 Mach-O | Resource bundle loading, BMP asset decoding, image view widget, font rendering | VERIFIED |
| Task Notes | `com.winios.tasknotes` | ARM64 Mach-O | Interactive UI widgets, button clicks, keyboard input, preferences, isolated storage, relaunch persistence | VERIFIED |

---

## 4. Developer SDK & Tooling

| Component | Path / Command | Purpose | Status |
|---|---|---|---|
| C Header SDK | `include/winios_sdk/winios_sdk.h` | ABI v1.2 syscalls (513-521), structs, constants | VERIFIED |
| Packaging Tool (`wpack`) | `tools/wpack.py` | Python CLI for compile, validate, pack, verify | VERIFIED |
| Compilation Backend | `zig cc -target aarch64-macos` | Freestanding ARM64 Mach-O generation on Windows host | VERIFIED |
| Package Format | `.wios` ZIP archive | Manifest, arm64 executable, assets/ bundle | VERIFIED |

---

## 5. Explicit Non-Claims & Boundaries

1. **No UIKit / SwiftUI:** WinIOS provides a custom lightweight guest UI framework for research and developer exploration. It does NOT claim to implement Apple's proprietary UIKit or SwiftUI.
2. **No SpringBoard / CoreAnimation:** The window manager and compositor are custom Windows-hosted components.
3. **No Metal:** Display output uses standard software framebuffers and optional Direct3D 11 host presentation backends, not Apple Metal emulation.
4. **No React Native / Flutter:** This campaign provides native ARM64 guest execution foundations; third-party cross-platform runtimes were not part of this campaign scope.
