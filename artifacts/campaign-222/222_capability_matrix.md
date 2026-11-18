# WinIOS Campaign 222 — Capability Matrix

## 1. System Capability & Research Maturity Assessment

| Capability Area | Status | Maturity Level | Evidence & Implementation Reference |
|---|---|---|---|
| **CPU Emulation & Execution** | **OPERATIONAL** | L4 (High Assurance) | Apple A16 Bionic (`iPhone15,4` / `D37AP` / `T8120`), ARM64 EL0 execution, 16 KB page granule translation, verified instruction retirement, architectural exception handling. |
| **Mach-O User Loader** | **OPERATIONAL** | L4 (High Assurance) | 64-bit Mach-O loader, page zero isolation, `__TEXT` (RX), `__DATA` (RW), `__LINKEDIT` (R), page alignment and segment mapping. |
| **Multi-Process Memory Isolation** | **OPERATIONAL** | L4 (High Assurance) | Independent L2 page tables per process, per-process ASID tagging (16-bit ASID), TLB invalidation by ASID, zero cross-process address aliasing. |
| **Category C Accelerated ABI** | **OPERATIONAL** | L4 (Production Ready) | ABI v1.6 (Syscalls 500..536), zero-copy pointer validation, discrete capability checks, strict Darwin ABI error mapping. |
| **Window Server & Multi-Window** | **OPERATIONAL** | L4 (High Assurance) | `WindowManager` spatial indexing, z-order stacking, visibility toggle (`533`), geometry management (`531`), raise-to-top (`532`), foreign hijack defense. |
| **Compositor & Presentation** | **OPERATIONAL** | L4 (High Assurance) | Front-to-back occlusion culling, dirty rect damage aggregation, `PresentationPacer` 60 Hz frame pacing, presentation telemetry. |
| **Input Routing & Provenance** | **OPERATIONAL** | L4 (High Assurance) | Windows Win32 coordinate translation, contact capture, focus switching on click, pointer drag tracking, focused keyboard routing. |
| **UI Toolkit v2 & 2D Graphics** | **OPERATIONAL** | L3 (Guest SDK Ready) | In-memory UI components (`winios_button_t`, `winios_label_t`, `winios_textinput_t`, `winios_checkbox_t`, `winios_progressbar_t`), embedded 8x8 font. |
| **System Clipboard Services** | **OPERATIONAL** | L4 (High Assurance) | Inter-process UTF-8 clipboard sharing (`534`, `535`) protected by `Capability::ClipboardRead` and `Capability::ClipboardWrite`. |
| **App Supervisor & Lifecycle** | **OPERATIONAL** | L4 (High Assurance) | Apple-inspired lifecycle state machine (`Created -> Starting -> Foreground -> Background -> Suspended -> Terminated`), automatic fault teardown. |
| **Isolated Sandbox Storage** | **OPERATIONAL** | L4 (High Assurance) | `/data/<bundle_id>/` per-app sandboxing, atomic write semantics, strict `..` path traversal rejection. |
| **Freestanding C Runtime** | **OPERATIONAL** | L4 (Freestanding SDK) | Native ARM64 CRT without host libc: `crt0.s`, `winios_runtime.c`, math, memory, formatting, syscall wrappers. |

---

## 2. Research Environment Disclosures

- **Host Environment:** Windows 11 x64, Visual Studio 2026, MSVC C++20.
- **Cross Toolchain:** Zig 0.17.0 (`-target aarch64-macos`).
- **Honest Capability Scope:** This is a Windows-hosted ARM64 iOS-inspired research execution platform. It does not claim binary compatibility with Apple proprietary frameworks (UIKit, SwiftUI, SpringBoard, Metal, CoreAnimation) or production iOS/Darwin kernels.
