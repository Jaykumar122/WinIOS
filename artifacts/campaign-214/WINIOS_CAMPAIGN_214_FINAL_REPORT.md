# WINIOS CAMPAIGN 214 — FINAL CLOSEOUT REPORT
## Guest UI Framework, Application Lifecycle, Rendering Reliability & Interactive Window Validation

**Campaign:** 214  
**Stages Executed:** 214.0 – 214.24 (25 sequential engineering stages)  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Baseline Commit:** `b7e0d93f8661fed8949449d3f6739b867e00a331` (Campaign 213 Closeout)  
**Host Toolchain:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`), CMake 3.31.6, CTest, Zig 0.17.0  
**Target Research Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), 16 KB translation granule  
**Test Pass Rate:** 711 / 711 test suites passed (100% pass rate, 0 warnings, 0 regressions)  
**Protected Paths Status:** 100% bit-for-bit verified match on all protected files  

---

## Executive Summary

Campaign 214 undertook an exhaustive, rigorous audit of Campaign 213's virtual display and interactive windowing infrastructure, resolved all root causes of identical screenshot hashes, and engineered a complete, reusable guest-side UI framework, an 8-state application lifecycle manager, and persistent typed application preferences.

The campaign culminated in a 15-step interactive desktop demonstration that generated 11 distinct, cryptographically verified framebuffer screenshots proving non-zero pixel mutations on guest state changes, deterministic state restore across application relaunches, and clean compositor recomposition upon window destruction.

---

## 1. Forensic Audit & Resolution of Campaign 213 Discrepancies

### 1.1 The Identical Screenshot Hashes Forensics (Stages 214.0 – 214.1)
An independent forensic audit recomputed the SHA-256 hashes of all Campaign 213 screenshots and inspected the underlying raw pixel buffers:
1. **Counter App (Screenshots 02, 03, 04):**
   - *Observation:* `screenshot_02`, `screenshot_03`, and `screenshot_04` had identical SHA-256 hashes (`57536a...`).
   - *Root Cause:* In Campaign 213, `counter_app.s` logged button tap console output and invoked `SYS_winios_surface_present` (504), but never mutated the pixels in its backing surface buffer.
   - *Fix in 214:* Implemented `CounterAppView` with `TextRenderer`. When the increment button is clicked, `count_label_->set_text("Count: 1")` invalidates the bounding box and renders mutated glyph pixels. The pixel buffer difference between count=0 and count=1 is exactly **279 mutated pixels**, producing a completely distinct SHA-256 hash (`01788115c584c7b43499103212e15e31fb06056c0c15c858281442a25b37083c`).
2. **Window Destruction Stale Frame (Screenshots 04 and 07):**
   - *Observation:* After window destruction, the screenshot still displayed the destroyed window.
   - *Root Cause:* `SYS_winios_window_destroy` (501) removed the window from `WindowManager`, but never triggered `Compositor::composite()`. The virtual display backbuffer was therefore never recomposited with the underlying Shell surface.
   - *Fix in 214:* Hardened `SYS_winios_window_destroy` and `AppLifecycleManager::terminate_app()` to immediately invoke `compositor_->composite(false)`. Destroying the Counter or Notes window now immediately restores the exact bit-for-bit Shell homescreen (`86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b`).

### 1.2 Frame Presentation Accounting Reconciliation (Stage 214.4)
- Campaign 213 reported 5 compositor frames but 7 host presented frames.
- Campaign 214 introduced `FrameAccounting` (`src/graphics/frame_accounting.h/.cpp`) tracking 8 distinct counters:
  `composed_frames` (5) + `repeated_frames_post_destruction` (2) = `host_presented_frames` (7).
- Arithmetic identity formally reconciled and validated in `stage214_4_frame_accounting_tests.exe`.

### 1.3 Category C Darwin ABI Specification (Stage 214.5)
- Standardized Category C custom Darwin syscalls (500..506) as ABI v1.0 (`kWiniosCategoryCAbiVersion = 0x00010000`):
  - 500: `SYS_winios_window_create`
  - 501: `SYS_winios_window_destroy`
  - 502: `SYS_winios_surface_update`
  - 503: `SYS_winios_surface_fill`
  - 504: `SYS_winios_surface_present`
  - 505: `SYS_winios_poll_event`
  - 506: `SYS_winios_set_window_title`
- Preserved strict Darwin calling convention: X16 = Syscall ID, X0..X5 arguments, Carry bit set in PSTATE on error with positive `errno` in X0.

---

## 2. Phase II: Guest UI Framework Architecture (Stages 214.6 – 214.12)

The new Guest UI Framework is an object-oriented, declarative retain-mode UI toolkit designed for freestanding ARM64 guest environments:

```
+-------------------------------------------------------------------+
|                        Guest UI Runtime                           |
+-------------------------------------------------------------------+
| Controls: Label | Button | Checkbox | Toggle | TextInput | etc.   |
+-------------------------------------------------------------------+
| Layout: VBox | HBox | AbsoluteLayout | Constraints | Alignment    |
+-------------------------------------------------------------------+
| View Hierarchy: View <---> ViewGroup (Cycle Rejection, Parent)   |
+-------------------------------------------------------------------+
| Graphics: RenderContext (Scissor Clip, Dirty Rect) | TextRenderer |
+-------------------------------------------------------------------+
| Theme: ThemeManager (Dark/Light, Metrics, Resource Isolation)     |
+-------------------------------------------------------------------+
```

1. **View & ViewGroup (`view.h`, `view_group.h`):** Base classes supporting hierarchic layout, bounds calculation, child-first hit testing, and cycle detection (preventing self or descendant additions).
2. **Layout Engine (`layout_engine.h`):** Implements flex-like linear layouts (`VBox`, `HBox`) with padding, margins, spacing, alignment (`Start`, `Center`, `End`, `Stretch`), and dimension constraints (`MatchParent`, `WrapContent`, fixed px).
3. **Text Rendering Engine (`text_renderer.h`):** High-performance 8x8 bitmap font renderer supporting string measurement, word wrapping, alignment, and zero-allocation glyph blitting.
4. **Interactive Controls Suite (`controls.h`):**
   - `Label`: Static or dynamic multiline text.
   - `Button`: Clickable button with hover/pressed visual states and click callbacks.
   - `Checkbox`: Toggleable state box with animated checkmark and label.
   - `Toggle`: iOS-style switch toggle.
   - `TextInput`: Editable text field with cursor tracking.
   - `ProgressBar`: Deterministic progress bar.
   - `ScrollView`: Scrollable viewport with clipping and programmatic offset.
5. **Partial Redraw & RenderContext (`render_context.h`):** 2D clipped rendering context enforcing bounding scissor rectangles and dirty region unions (`union_rect`) to minimize redraw overhead.
6. **Theme & Resource Isolation (`theme.h`):** Theme management providing system palettes (Dark/Light mode) and tracking UI allocations per-PID with automatic teardown on process exit.

---

## 3. Phase III: Input, Gestures & Application Lifecycle (Stages 214.13 – 214.19)

1. **Event Loop (`event_loop.h`):** Bounded UI event processing loop handling timers, high-priority tasks, and input events.
2. **Gesture Recognizer (`gesture_recognizer.h`):** Recognizes discrete and continuous gestures: `Tap`, `DoubleTap`, `LongPress`, `Pan`, `Swipe`, and `Cancel`.
3. **Event Dispatcher (`event_dispatcher.h`):** Dispatches touches with active pointer capture, focus cycling, and hit interception on disabled views.
4. **Application Lifecycle Manager (`app_lifecycle_manager.h`):**
   - Enforces an explicit 8-state machine:
     `Created` -> `Starting` -> `Foreground` <-> `Background` -> `Suspended` -> `Resuming`, `Terminating` -> `Terminated`.
   - On termination, performs complete resource reclamation: window destruction, surface destruction, compositor surface removal, input router clean-up, theme resource release, and focus restoration.
5. **Package Manifest Validator (`app_package.h`):** Validates package metadata, memory limits, surface quotas, and verifies binary SHA-256 hashes against manifest declarations.
6. **Application Preferences Store (`app_preferences.h`):** Thread-safe, isolated per-app key/value storage (bool, int, float, string) with JSON snapshot serialization and deterministic recovery on app relaunch.

---

## 4. Phase IV: Adversarial Security & 15-Step Desktop Demonstration (Stages 214.20 – 214.24)

### 4.1 32-Vector Adversarial Security Suite (Stage 214.22)
All 32 adversarial test vectors passed with zero violations:
- Vectors 1–3: Cross-process window destroy, surface destroy, and surface pixel copying rejected with `EBADF` / `AccessDenied`.
- Vectors 4–5: Cross-process preferences isolation verified (reads return `nullopt`, writes isolated).
- Vectors 6–7: Unknown syscall 999 and out-of-bounds Category C syscall 507 rejected with `ENOSYS`.
- Vectors 8–10: Invalid window geometry (w=0, h=0) and invalid title pointers safely rejected with `EINVAL` / default fallback.
- Vectors 11–14: Unmapped buffer pointers, truncated sizes, and stale window IDs rejected with `EFAULT`, `EINVAL`, `EBADF`.
- Vectors 15–16: Undersized poll event buffers (< 32 bytes) and unmapped buffers rejected with `EINVAL` and `EFAULT`.
- Vectors 17–20: Window destroy ID 0, double destroy, and cross-process set window title rejected with `EBADF`.
- Vectors 21–26: Malformed manifests (empty app_id, illegal characters, missing fields, excessive memory/surfaces, tampered binary hash) rejected.
- Vectors 27–29: Illegal lifecycle transitions (`Created` -> `Resuming`, `Terminated` -> `Foreground`, `Suspended` -> `Starting`) rejected.
- Vectors 30–31: View hierarchy cycle self-addition and descendant-addition rejected.
- Vector 32: Premature process exit during active touch capture cleanly clears dispatcher capture without crashing.

### 4.2 15-Step Desktop Demonstration & Screenshot Forensic Verification (Stage 214.23)
Executed the full 15-step workload in `stage214_23_desktop_e2e_tests.exe`:

| Step | Operation | Result | Screenshot Filename | SHA-256 Hash |
| :--- | :--- | :--- | :--- | :--- |
| **1** | Headless mode init | Success (`is_headless() == true`) | — | — |
| **2** | Desktop window init | Success (`is_open() == true`) | — | — |
| **3** | Shell launch at EL0 | Success (PID 1, Window ID 1) | — | — |
| **4** | Capture Shell homescreen | Composited & Presented | `screenshot_01_shell_homescreen.bmp` | `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` |
| **5** | Launch Counter App (PID 2) | Window ID 2 at Z=1, count=0 | `screenshot_02_counter_app_launched.bmp` | `57536a1550bf967a24a3c8b36db414c00e554ce3ccc8e8c8874c61b3cd4d0dbd` |
| **6** | Increment Counter | Count=1 (279 mutated px) | `screenshot_03_counter_app_incremented.bmp` | `01788115c584c7b43499103212e15e31fb06056c0c15c858281442a25b37083c` |
| **7** | Background Counter | Opacity 0.40 | `screenshot_04_counter_app_backgrounded.bmp` | `e47dceea4e3db6a8961f220c33051422c64850b50332252a3c1d643c8fc6133a` |
| **8** | Close Counter App | Recomposited Shell | `screenshot_05_return_to_shell.bmp` | `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` |
| **9** | Launch Notes App (PID 3) | Window ID 3 at Z=1 | `screenshot_06_notes_app_launched.bmp` | `dc7dc3208e44fe25c33665f7615f41c058a486a43363d546093f25f17b84ba66` |
| **10** | Toggle Checkbox Item 3 | Checked (576 mutated px) | `screenshot_07_notes_app_checked.bmp` | `06cab01e129e77b611cceb300895cba29ba7e9d7158cae8ef7acc35454f7d98b` |
| **11** | Scroll Notes Content | Offset +80px | `screenshot_08_notes_app_scrolled.bmp` | `18f687dcedd6d6c21798fad85023d82f8839fdacff944d1b58d1af639a994740` |
| **12** | Close Notes App | Recomposited Shell | `screenshot_09_notes_closed_shell.bmp` | `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` |
| **12d**| Relaunch Notes (PID 4) | Restored preferences | — | — |
| **13** | Verify Persisted State | Item 3 checked restored | `screenshot_10_notes_relaunch_persisted.bmp` | `06cab01e129e77b611cceb300895cba29ba7e9d7158cae8ef7acc35454f7d98b` |
| **14** | Final return to Shell | Clean recomposition | `screenshot_11_shell_final.bmp` | `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` |
| **15** | Orderly Shutdown | Zero leaks, 0 windows | — | — |

---

## 5. Protected Paths Verification

The four mandatory protected paths were audited and verified:
1. `D:\WinISO26\WinISO26_README.md`: **DOES NOT EXIST** (Verified).
2. `D:\WinISO26\WinIOS_README.md`: SHA-256 = `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` (100% bit-for-bit match).
3. `D:\WinISO26\WinIOS\docs\progress.md`: SHA-256 = `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` (100% bit-for-bit match).
4. `D:\WinISO26\Inferno-Research/`: **UNTOUCHED** (0 files modified).

---

## 6. Honest Capability Boundaries

WinIOS is a Windows-hosted research environment for ARM64 Darwin execution:
- **What is implemented:** Freestanding ARM64 userspace execution, Category C Darwin ABI v1.0, 16 KB virtual memory management, multi-process scheduling, virtual display surface management, multi-layer alpha compositor, dual-mode host presentation window (Win32 GDI / headless), retain-mode UI framework (View, ViewGroup, VBox, HBox, TextRenderer, Controls), gesture recognition, focus management, 8-state application lifecycle management, and typed key/value persistence.
- **What is NOT claimed:** This does not represent production Apple iOS, genuine UIKit / SwiftUI, Apple Metal graphics acceleration, or production SpringBoard services.

---

## 7. Artifacts Index

All Campaign 214 artifacts are archived in `D:\WinISO26\WinIOS\artifacts\campaign-214/`:
- `WINIOS_CAMPAIGN_214_FINAL_REPORT.md`
- `214_sha256_manifest.txt`
- `214_capability_matrix.md`
- `214_test_results.md`
- `214_guest_execution_trace.md`
- `214_screenshot_comparison.md`
- Screenshots 01 through 11 (`.bmp`)
