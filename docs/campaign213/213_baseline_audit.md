# WinIOS Campaign 213: Baseline Audit & Repository Integrity

**Campaign:** 213  
**Stage:** 213.0  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target Research Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host:** Windows 11 x64  
**Primary Toolchain:** Visual Studio 2026 / MSVC C++20 (`/W4 /WX /permissive-`)  
**Cross Compiler:** Zig 0.17.0  
**Date:** 2026-11-17  

---

## 1. Baseline Verification & Git State

- **Branch:** `campaign-191r`
- **Required Baseline Commit:** `d065badb7fe4fb1bbc8e80939bfb842db8ee50a7` (Verified matching HEAD)
- **Git Working Tree:** Clean (verified via `git status`)
- **Protected Assets Integrity:**
  - `D:\WinISO26\WinIOS_README.md`: `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` (Verified unchanged)
  - `D:\WinISO26\WinIOS\docs\progress.md`: `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` (Verified unchanged)
  - `D:\WinISO26\WinISO26_README.md`: Verified not present
  - `D:\WinISO26\Inferno-Research/`: Verified preserved and untouched

---

## 2. Inventory of Existing Graphics & Display Components

Prior campaigns (notably Stage 127 and 137) established foundational components:

1. **`AppleDisplay` (`src/device/apple_display.h`, `src/device/apple_display.cpp`):**
   - Virtual device implementing MMIO registers (`kRegWidth`, `kRegHeight`, `kRegDepth`, `kRegRowBytes`, `kRegFbBaseLow`, `kRegFbBaseHigh`, `kRegStatus`, `kRegControl`).
   - Default iPhone 15 resolution: 1179 x 2556, 32-bit ARGB, 4716 row bytes.
   - Linear framebuffer buffer, pixel get/set, test patterns, and boot splash.

2. **`WindowServerCompositor` & `IOSurface` (`src/graphics/window_server.h`, `src/graphics/window_server.cpp`):**
   - `PixelFormat` (`B8G8R8A8_UNORM`, `R8G8B8A8_UNORM`).
   - `Rect` geometry and `DamageTracker` for aggregating dirty bounding boxes.
   - `IOSurface` offscreen backing store with read/write pixel access and rect fills.
   - `WindowServerLayer` with layer IDs, client task IDs, Z-order, opacity, and visibility.
   - `DirectXPresentationAdapter` staging buffer and presentation statistics.
   - `WindowServerCompositor` layer composition pipeline.

3. **`DisplayWindow` (`src/ui/display_window.h`, `src/ui/display_window.cpp`):**
   - Win32 desktop presentation window (`CS_HREDRAW | CS_VREDRAW`, `WM_PAINT`, `WM_MOUSEMOVE`, `WM_LBUTTONDOWN`, `WM_LBUTTONUP`).
   - Aspect ratio preservation, coordinate scaling, touch event injection, and BMP screenshot capture.
   - Headless mode for automated non-GUI CI and tests.

---

## 3. Scope and Gaps for Campaign 213

To meet Campaign 213 requirements, the following components must be designed and implemented:

1. **Configurable Display Architecture (`VirtualDisplay`, `DisplayConfiguration`):**
   - Configurable dimensions, orientation (portrait/landscape), pixel formats (RGBA8888, BGRA8888, RGB565), and display scale.
2. **Guest Surface Ownership & Memory Accounting:**
   - Explicit guest PID ownership of surfaces (`DisplaySurface`), per-process surface limits and quotas, and W^X memory security.
3. **Guest Display Syscalls & IPC:**
   - Dedicated WinIOS guest syscalls for window creation, surface destruction, content updates, damage notification, and input polling.
4. **Virtual Touch Input Pipeline:**
   - `VirtualInputDevice` with multi-touch event queue (`TouchEvent`, `TouchPhase`: Begin, Move, End, Cancel).
   - Host-to-guest coordinate mapping, hit testing, frontmost surface selection, and focus routing.
5. **Freestanding ARM64 Mach-O Guest Shell & Applications:**
   - Genuine ARM64 assembly shell (`guest_shell.macho`) executing at EL0, rendering a smartphone-style grid of application icons.
   - Two interactive guest applications (`counter_app.macho` and `notes_app.macho`) receiving touch events and updating surfaces.
   - Application launch and exit cycle supervised by PID 1.
6. **Deterministic Snapshot & Replay:**
   - Full serialization and restoration of virtual display, surface tables, layer Z-orders, and touch queues.
7. **25-Vector Adversarial Security Suite:**
   - Comprehensive protection against buffer overruns, invalid surface handles, cross-process access, corrupted formats, and resource exhaustion.
