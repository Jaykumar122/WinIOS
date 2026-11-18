# Campaign 222 — Stage 222.2 Interactive Runtime Architecture & Formal Data Contracts Report

## Overview
Stage 222.2 formalizes the memory contracts, data structures, alignments, and lifecycle specifications governing the WinIOS interactive runtime environment. All structures are explicitly sized, packed with 8-byte alignment boundaries, and compatible across both MSVC C++20 and ARM64 freestanding C.

## Core Architectural Contracts

### 1. Window Server & Handle Isolation
- **Handle Representation**: Window IDs (`winios_wid_t`) are opaque 32-bit positive integers managed exclusively by the host Window Server (`WindowManager`).
- **Process Ownership**: Each window record binds an owner PID (`owner_pid`). Any syscall attempting to access, manipulate, or render to a window whose `owner_pid` does not match the caller PID is rejected with `EBADF` or `EPERM`.
- **Z-Order Management**: Windows are stacked along a monotonic signed integer axis (`z_order`). Active and focused windows are elevated to top z-order (`z_order = 0` or higher).

### 2. Surface & Framebuffer Contract
- **Format**: 32-bit ARGB8888 (`0xAARRGGBB`), with 4 bytes per pixel.
- **Stride**: Surface stride is guaranteed to be a multiple of 16 bytes:
  $$\text{stride} = \text{width} \times 4$$
- **Damage Tracking**: Bounded dirty rectangle lists (`winios_dirty_rect_t`) specify updated subregions. The Compositor processes dirty rects and composites only non-occluded damaged regions.

### 3. Event Queue & Event Loop v2
- **Unified Structure**: `winios_event_v2_t` is a fixed 64-byte record:
  - 32 bytes of event payload (`type`, `contact_id`, `x`, `y`, `key_code`, `modifiers`, `wheel_delta`, `window_id`).
  - 8-byte microsecond timestamp (`timestamp_us`).
  - 24-byte hardware input provenance record (`winios_input_provenance_t`).
- **Provenance Auditing**: Distinguishes physical host Win32 messages from simulated or test-injected inputs.

### 4. Application Lifecycle & Supervision
- **States**: `Uninitialized` $\rightarrow$ `Starting` $\rightarrow$ `Running` $\leftrightarrow$ `Suspended` $\rightarrow$ `Terminated` / `Faulted`.
- **Zero-Leak Cleanup**: Abrupt process exit or unhandled fault automatically reclaims all allocated window handles, display surfaces, timers, and IPC ports.

## Structure Size and Alignment Summary
| Structure | Size (bytes) | Alignment | Purpose |
|---|---|---|---|
| `winios_rect_t` | 16 | 4 | Bounding rectangles |
| `winios_window_geometry_t` | 32 | 8 | Window position, frame, and state |
| `winios_surface_desc_t` | 32 | 8 | Framebuffer parameters |
| `winios_dirty_rect_t` | 16 | 4 | Sub-region damage tracking |
| `winios_input_provenance_t` | 24 | 8 | Input event auditability |
| `winios_event_v2_t` | 64 | 8 | Full event queue message |
| `winios_presentation_stats_t` | 48 | 8 | Compositor metrics and frame pacing |
| `winios_clipboard_req_t` | 16 | 8 | Cross-process clipboard exchange |

All structures are defined in `src/runtime/interactive_runtime_contract.h`.
