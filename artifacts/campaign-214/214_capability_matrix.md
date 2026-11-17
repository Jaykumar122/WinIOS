# WinIOS Campaign 214 — Capability Matrix

## Target Profile: iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)
**Host Environment:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`)  
**Translation Granule:** 16 KB  
**ABI Standard:** Category C ABI v1.0 (Syscalls 500..506) with DarwinStrict error handling

---

## 1. Subsystem Capability Inventory

| Subsystem | Campaign 213 Baseline | Campaign 214 Status | Implementation & Validation |
| :--- | :--- | :--- | :--- |
| **Virtual Display & Compositor** | Basic layer blending, stale window bug | Hardened Compositor | Automatic recomposition on window destruction; verified bit-for-bit restoration of shell (`hash_05 == hash_01`). |
| **Host Presentation Engine** | Headless-only verified | Dual Mode (Headless + Desktop) | `DisplayWindow` with `PresentationMode::Headless` and `PresentationMode::DesktopWindow`, Win32 `WM_PAINT`, `StretchDIBits` backbuffer, `WM_SIZE` geometry scaling. |
| **Frame Accounting** | Discrepancy (5 comp vs 7 pres) | Exact Accounting Audit | `FrameAccounting` tracks 8 counters. Proved arithmetic identity: 5 composed + 2 repeated = 7 presented. |
| **Category C Darwin ABI** | Undocumented raw syscalls | Formally Specified Category C v1.0 | Syscalls 500..506 versioned (`0x00010000`). DarwinStrict carries errno in X0 with PSTATE Carry flag set. |
| **View Hierarchy** | None (Raw pixel drawing) | Reusable `View` & `ViewGroup` | Hierarchical composite pattern, parent pointers, cycle detection (self/descendant rejection), child-first hit testing. |
| **Layout Engine** | None | `LayoutEngine` (`VBox`, `HBox`, `Absolute`) | Padding, margins, spacing, alignment (Start, Center, End, Stretch), auto-wrap, min/max dimension constraints. |
| **Text Rendering** | None | `TextRenderer` (8x8 Glyph Engine) | Bitmap font metrics, measurement, line wrapping, multiline alignment, zero allocation bounding box calculation. |
| **UI Controls** | None | Full Control Suite | `Label`, `Button`, `Checkbox`, `Toggle`, `TextInput`, `ProgressBar`, `ScrollView` with stateful rendering. |
| **Render Invalidation** | Full redraw only | Clipped 2D Context & Dirty Rects | `RenderContext` with scissor clipping, `union_rect` dirty region calculation, and partial redraw pass. |
| **Theme & Resource Manager** | None | System Theme & Isolation | Dark/Light palettes, system metrics (DPI, corner radius, touch target), per-PID resource tracking and cleanup. |
| **Event Loop & Gestures** | Raw syscall poll loop | Unified `EventLoop` & `GestureRecognizer` | Bounded queue, priority dispatch, tap, double tap, long press, pan, swipe, and cancel gesture detection. |
| **Focus & Touch Dispatcher** | Global input router | Focus & Capture Dispatcher | `EventDispatcher` manages active touch capture, keyboard focus cycling, disabled view hit interception. |
| **Application Lifecycle** | Ad-hoc process states | 8-State Formal Lifecycle Machine | `AppLifecycleManager` enforces legal state transitions: Created -> Starting -> Foreground <-> Background -> Suspended -> Resuming, Terminating -> Terminated. |
| **Package Manifests** | None | `AppPackageValidator` | Strict validation of `AppManifest`: app_id format, memory limits, surface limits, and binary SHA-256 integrity. |
| **App Preferences Store** | None | `AppPreferencesManager` | Isolated per-app typed key/value persistence (bool, int, float, string) with JSON snapshot serialization & restore. |
| **Adversarial Security** | Basic bounds checks | 32-Vector Adversarial Suite | Comprehensive security verification across cross-PID memory/window/surface attacks, unmapped VAs, lifecycle bypass, and hierarchy cycles (32/32 vectors passed). |
