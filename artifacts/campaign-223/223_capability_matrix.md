# WinIOS Campaign 223 — Capability Matrix & Disclosures (Stage 223.24)

## 1. Executive Summary

In adherence to strict engineering honesty, this document defines the exact capabilities, boundaries, and limitations established by Campaign 223.

---

## 2. Verified Capabilities vs Reality Disclosures

| Domain | Verified Capability in Campaign 223 | Explicit Limitation / Non-Goal |
| :--- | :--- | :--- |
| **Guest UI Framework** | `winios_app_t` lifecycle with inversion of control, reentrancy guard, event loop | Not production UIKit, AppKit, or SwiftUI. No Objective-C runtime or Swift ABI. |
| **Layout Engine** | `winios_layout_node_t` flexbox (Row/Col/Absolute) with flex weights and zero heap allocation | Not CSS Flexbox, Yoga, or AutoLayout constraint solver. |
| **UI Components** | TextArea (multiline), Slider, Dialog, Menu, Focus Manager | Not full-featured RichText, WebView, WebKit, or CoreText. |
| **Font Rendering** | 8x8 monochrome bitmap glyph matrix (`font8x8.h`) for printable ASCII | Not TrueType, OpenType, FreeType, HarfBuzz, or sub-pixel antialiasing. |
| **Window Management** | Multi-window desktop with non-client hit testing, title bars, and Z-stacking | Not SpringBoard, macOS Quartz Compositor, or Wayland compositor. |
| **Display & Compositor**| 1179x2556 virtual display, partial damage tracking, frame skipping, occlusion culling | Not Apple Metal, Direct3D 12, or Vulkan hardware GPU pipeline. |
| **Input Delivery** | InputRouter with physical/simulated provenance tags, pointer capture, hit testing | Not native Apple MultiTouch IOHID driver or CoreMotion sensors. |
| **IPC & Clipboard** | SystemClipboard with 64 KB limit, capability enforcement, and audit logs | Not NSPasteboard or Apple Universal Clipboard / CloudKit sync. |
| **Tooling & Packages** | Guest SDK v1.7, WIOSPKG2 container format, Zig 0.17.0 cross-compilation | Not Xcode, CocoaPods, Swift Package Manager, or Apple App Store. |
