# WinIOS Campaign 216 — Capability & Architecture Matrix

## 1. System Overview
- **Campaign:** 216 — Multi-Window Desktop, Verified Windows Input, Renderer Architecture & Guest Event Services
- **Host Target:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`), CMake 3.31.6
- **Guest Target:** ARM64 / ARM64e research profile, iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 `T8120`), 16 KB guest translation granule
- **Baseline Commit:** `c5509421bb723b1910281d16134f846962141b13` (Campaign 215 closeout)
- **Status:** All 25 Stages (216.0–216.24) Implemented and 100% Verified.

---

## 2. Comprehensive Capability Matrix

| Subsystem | Feature / Capability | Implementation Status | Verification Level | Architectural Role |
| :--- | :--- | :---: | :---: | :--- |
| **Input Pipeline** | Win32 Mouse Processing (`WM_MOUSEMOVE`, `WM_LBUTTONDOWN`, `WM_RBUTTONDOWN`, `WM_MOUSEWHEEL`) | Complete | Unit & E2E Tested | Captures host mouse events with high precision |
| **Input Pipeline** | Coordinate Translation & Clamping | Complete | Unit & E2E Tested | Maps arbitrary host window resolutions to virtual display |
| **Input Pipeline** | Win32 Keyboard Processing (`WM_KEYDOWN`, `WM_KEYUP`, `WM_CHAR`) | Complete | Unit & E2E Tested | Full virtual-key mapping, repeat count, modifier flags |
| **Input Pipeline** | Focus Management & Mouse Capture | Complete | Unit & E2E Tested | Click-to-focus, drag capture with deterministic release |
| **Input Pipeline** | Input Provenance Telemetry | Complete | Unit & E2E Tested | 5-way origin classification (`PhysicalWindowsInput`, `WindowsMessageInjected`, etc.) |
| **Input Pipeline** | Input Security & Queue Isolation | Complete | Unit & Fuzz Tested | Anti-theft enforcement, destroyed window rejection, queue backpressure |
| **Desktop / Windows**| Multi-Window Stacking Order (Z-Order) | Complete | Unit & E2E Tested | Front-to-back z-ordering with `raise_to_top` and `lower_to_bottom` |
| **Desktop / Windows**| Always-on-Top Floating Windows | Complete | Unit & E2E Tested | Dedicated stacking tier for tool palettes, dialogs, and overlays |
| **Desktop / Windows**| Non-Client Window Decorations | Complete | Unit & E2E Tested | 28px title bar, active/inactive headers, close/minimize/maximize buttons |
| **Desktop / Windows**| Non-Client Zone Hit-Testing | Complete | Unit & E2E Tested | Sub-rectangle hit discrimination for non-client interactions |
| **Desktop / Windows**| Window Movement & Resizing Interaction | Complete | Unit & E2E Tested | Window dragging and resizing with bounds clamping (min/max dimensions) |
| **Desktop / Windows**| Per-Window Damage Tracking | Complete | Unit & E2E Tested | Dirty rectangle unions minimizing redraw area |
| **Desktop / Windows**| Multi-Window Compositor | Complete | Unit & E2E Tested | Front-to-back traversal, damage clipping, and background composition |
| **Desktop / Windows**| Occlusion Optimization | Complete | Unit Tested | Skipping completely covered lower surfaces during redraw |
| **Desktop / Windows**| Deterministic Window Animator | Complete | Unit & E2E Tested | Step-driven frame and alpha transitions for open/minimize/restore |
| **Renderer Subsystem**| `IRenderer` Interface Abstraction | Complete | Unit Tested | Decouples window presentation from underlying host graphics APIs |
| **Renderer Subsystem**| Software Reference Renderer | Complete | Unit Tested | Fallback CPU stretch-blit rasterizer using standard GDI / DC blitting |
| **Renderer Subsystem**| Direct3D 11 Host Presentation Backend | Complete | Unit Tested | Hardware swapchain, dynamic staging texture blits, VSync control |
| **Renderer Subsystem**| Device Loss Recovery Pipeline | Complete | Unit Tested | Graceful recovery and zero-loss fallback to software renderer |
| **Renderer Subsystem**| Frame Presentation Pacing & Diagnostics| Complete | Unit Tested | Jitter tracking, frame time histograms, presentation telemetry |
| **ABI & SDK** | Category C ABI v1.1 Specification | Complete | Unit & E2E Tested | Version `0x00010100`, syscalls 500..512 fully implemented |
| **ABI & SDK** | Syscall 508: `kSysWiniosWindowGetGeometry`| Complete | Unit Tested | Frame, z-order, state, focus, always-on-top query |
| **ABI & SDK** | Syscall 509: `kSysWiniosWindowSetGeometry`| Complete | Unit Tested | Programmatic window positioning, resizing, and tier elevation |
| **ABI & SDK** | Syscall 510: `kSysWiniosWindowSetFocus` | Complete | Unit Tested | Direct window activation and foreground promotion |
| **ABI & SDK** | Syscall 511: `kSysWiniosWindowList` | Complete | Unit Tested | Enumerate caller-owned windows in structured or array format |
| **ABI & SDK** | Syscall 512: `kSysWiniosCreateTimer` | Complete | Unit Tested | Virtual guest timer registration with per-process quotas |
| **ABI & SDK** | C SDK Header (`include/winios_sdk/winios_sdk.h`)| Complete | Unit Tested | Binary-compatible structures with strict 8-byte packing |
| **Guest Runtime** | Guest Event Services | Complete | Unit & E2E Tested | Virtual time progression, timer firing, synthetic event delivery |
| **Application Layer**| `MultiWindowEditorApp` | Complete | Unit & E2E Tested | Dual-window app (Document Editor + Floating Tool Palette) |
| **Application Layer**| Document State Persistence | Complete | Unit & E2E Tested | IsolatedStorage integration with atomic write and verification |

---

## 3. Explicit Research Disclaimers & Non-Claims
1. **Research Runtime Profile:** WinIOS is an exploratory research runtime hosted on Windows 11 x64, investigating ARM64/Darwin system call emulation and userspace graphics architectures.
2. **No Production iOS Compatibility:** WinIOS does NOT execute closed-source Apple binaries, production iOS system components, SpringBoard, UIKit, SwiftUI, or CoreAnimation.
3. **Host Presentation Direct3D 11:** The Direct3D 11 renderer is strictly a host-side frame presentation backend for blitting the composited virtual display buffer to a Windows desktop window. It does NOT implement Apple Metal emulation, GPU virtualization, or guest GPU passthrough.
4. **Deterministic Sandboxing:** All operations operate within controlled sandbox boundaries with explicit capability checks and isolated per-process virtual storage quotas.
