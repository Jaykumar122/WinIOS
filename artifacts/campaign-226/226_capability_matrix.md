# Campaign 226: Capability Matrix & System Boundaries

## 1. Architectural Capability Matrix

| Subsystem | Status | Scope / Verification Level | Campaign Boundary / Limitations |
|:---|:---|:---|:---|
| **Window Manager Core** | Complete | 64-bit generation handles, PID ownership, z-stack reordering, non-client hit testing | Max 64 concurrent windows per session; in-memory state. |
| **Compositor & Damage Tracking** | Complete | Rectangular damage accumulation, dirty-rect clearing, layer stacking, non-client chrome | Software blitter / pixel pipeline; no hardware GPU shaders. |
| **Input Routing & Focus** | Complete | Pointer hit-testing, move coalescing, keyboard focus isolation, text input submission | Focused window keyboard model; no global hotkey interception. |
| **Window Dragging & Controls** | Complete | Real-time titlebar dragging, boundary clamping, close/min/max non-client controls | Titlebar height fixed at 24px; desktop-clamped bounds. |
| **Notification Overlay** | Complete | Up to 3 floating toast banners, category badges, auto-clear damage redraw | Toast layout fixed at top-right ($240\times 50$ px). |
| **Category C ABI v2.0** | Complete | Syscalls 561..570 fully dispatched with copyin/copyout boundary checks | Bounded to Category C syscall numbers 561–570. |
| **Guest SDK v2.0** | Complete | C runtime headers (`window.h`, `events.h`), freestanding runtime wrappers | Freestanding C runtime; no objective-c / Swift runtime. |
| **Native ARM64 Workloads** | Complete | 2 freestanding Mach-O binaries (`campaign226_multiwindow_app`, `campaign226_interactive_peer`) | Freestanding ARM64 Mach-O; not production UIKit or SwiftUI. |
| **Multi-Process Concurrency** | Complete | Isolated address spaces, capability security, cross-process clipboard sharing | Cooperative priority scheduler; no preemptive timer interrupts. |
| **Adversarial Hardening** | Complete | Forged handle rejection, ABA generation defense, 128-event queue bounding | Deterministic simulation profile; hardened against memory abuse. |

## 2. Research Profile & Environment Integrity
- **Target Profile**: iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), ARM64 EL0, 16 KB translation granule.
- **Reference Kernel Baseline**: pinned public XNU `xnu-10002.1.13`.
- **Honest Disclosure**: WinIOS is a native Windows 11 x64 research platform executing freestanding ARM64 guest applications. It does NOT implement Apple proprietary frameworks (UIKit, SwiftUI, Cocoa Touch, AppKit) or proprietary App Store DRM.
