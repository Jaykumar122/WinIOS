# WinIOS Campaign 215 — Capability Matrix & Architectural Boundaries

## 1. Executive Summary & Honest Research Language

WinIOS is a Windows-hosted, research-oriented ARM64 execution and runtime environment inspired by Darwin and XNU architectures. It provides an isolated, sandboxed application execution layer with genuine ARM64 translation, custom Mach-O loader, Darwin-like process scheduling, Category C ABI v1.0 system extensions, a lightweight user-interface framework, transactional packaging, and sandboxed private storage.

WinIOS is **not** production iOS, does **not** run proprietary Apple frameworks (such as UIKit, SwiftUI, CoreGraphics, Metal, or Foundation), does **not** require Apple developer certificates, and does **not** rely on proprietary Apple silicon firmware or hardware coprocessors.

---

## 2. Platform Capability Matrix

| Capability Subsystem | Current WinIOS Implementation State | Production iOS Analog / Difference |
| :--- | :--- | :--- |
| **Host Operating System** | Windows 11 x64, native MSVC C++20 (`/W4 /WX /permissive-`) | macOS / iOS Darwin Kernel |
| **CPU Architecture** | ARM64 (A16 Bionic `T8120` research profile) with 16 KB pages | ARM64 / ARM64e with hardware PAC and APRR |
| **Exception Levels** | EL0 (Userspace), EL1 (Kernel runtime model) | Full EL0..EL3 hardware execution |
| **Syscall ABI** | Category C ABI v1.0 (Syscalls 500..507) + DarwinStrict / Fast modes | Standard XNU BSD & Mach Trap Table |
| **Memory Management** | Demand paging, 16 KB virtual page tables, `GuestMemoryValidator` | XNU `vm_map`, pmap, physical aperture |
| **Process Model** | Mach-O user loader, BSD process hierarchy (`launchd` PID 1, apps PID >= 2) | Full XNU BSD tasks, Mach tasks, posix spawn |
| **Packaging & Distribution**| WinIOS Container Format v1.0 (`.wios`), SHA-256 manifest, transactional installer | iOS App Bundle (`.ipa`), FairPlay DRM, codesign |
| **Application Isolation** | Sandboxed private storage (`/data/<bundle_id>/`), atomic writes, quota | iOS Data Sandbox, Container Manager, sandboxd |
| **Permissions & Security** | Bitmask capability manager (`display.window`, `storage.private`, etc.) | TCC (Transparency, Consent & Control), entitlements |
| **Display & Compositor** | `WindowManager`, `DisplaySurface`, `DisplayWindow` with Win32 GDI capture | QuartzCore, WindowServer, RenderServer, Metal |
| **Input Subsystem** | Bounded `HostInputQueue`, `CoordinateNormalizer`, gesture recognizer | IOHIDFamily, MultitouchSupport, UIKit Touch Event Loop |
| **Inter-Process Data** | Capability-gated `SystemClipboard` with caller audit logging | UIPasteboard, Pasteboard Server (`pbd`) |
| **Developer SDK** | C-compatible SDK (`winios_sdk.h`) with Category C syscall wrappers | iOS SDK / Xcode toolchain (`UIKit`, `Foundation`) |

---

## 3. Strict Boundary Guarantees

1. **Guest Isolation**: Guest code cannot access host memory or file systems outside the virtualized VFS (`/Applications/`, `/data/`, `/tmp/`).
2. **Buffer Validation**: All guest pointers passed into syscalls 500..507 undergo 8-point validation by `GuestMemoryValidator` before dereferencing.
3. **No Apple Proprietary Code**: All code, shaders, fonts, layouts, and syscall dispatchers are built cleanly from first principles.
4. **Reproducibility**: All unit tests, security vectors, and desktop E2E flows execute deterministically on any standard Windows 11 MSVC machine without external dependencies.
