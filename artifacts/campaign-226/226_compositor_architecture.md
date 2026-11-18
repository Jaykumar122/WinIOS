# Campaign 226: Desktop Compositor & Damage Tracking Architecture

## 1. Overview
Stage 226.4 and 226.5 established the multi-window composition engine and fine-grained rectangular damage tracking for WinIOS. The compositor combines window surfaces, non-client window decorations (titlebars, borders, control buttons), system overlays (notifications, cursor), and the desktop background into the primary display framebuffer (`VirtualDisplay`).

## 2. Layer Stacking & Z-Order Model
The compositor processes windows along a strictly ordered Z-stack:
1. **Desktop Background Layer**: Solid or themed backdrop (cleared to background color when invalidated).
2. **Normal Window Stack (Bottom to Top)**: Ordered by z-order index. Closed and minimized windows are culled early.
3. **Topmost / Modal Window Layer**: Preserves z-priority over standard normal windows.
4. **Desktop Overlays**: Floating notification toasts managed by `VirtualNotificationService` and `DesktopController`.
5. **Cursor Layer**: Hardware or software-rendered pointer cursor.

```
+-------------------------------------------------------------+
| Layer 4: System Overlay (Notification Toasts, Cursor)       |
+-------------------------------------------------------------+
| Layer 3: Topmost / Tool Windows                             |
+-------------------------------------------------------------+
| Layer 2: Normal Application Windows (Z-Order 0 .. N)        |
+-------------------------------------------------------------+
| Layer 1: Desktop Background Framebuffer (800x600)           |
+-------------------------------------------------------------+
```

## 3. Damage Accumulation & Dirty Rect Invalidation
To minimize redundant pixel writes and host memory bus pressure, the compositor tracks rectangular regions of interest:
- Each window maintains a cumulative bounding box of dirty areas (`damage_rect_`).
- Syscall `SYS_WINIOS_WINDOW_INVALIDATE_RECT` (565) allows guest applications to submit dirty rectangles relative to their window client coordinates.
- Bounds are automatically clamped to the window dimension $[0, \text{width}] \times [0, \text{height}]$ to prevent invalid memory indexing.
- The compositor aggregates window damage into global screen-space damage rects during compositing.
- When `composite(force_full_redraw = false)` is invoked, only dirty windows transfer pixel data to the backbuffer. Once composited, `clear_damage()` resets the window's dirty region.

## 4. Non-Client Decorations & Window Chrome
The compositor is responsible for non-client rendering:
- **Titlebar**: 24-pixel height banner displaying window title string in Segoe UI / TrueType font.
- **Window Controls**:
  - Close button: Top-right circle/square (red), emits `WindowCloseRequest` event.
  - Maximize button: Toggle between maximized ($[0, 26, \text{disp\_w}, \text{disp\_h} - 56]$) and restored bounds.
  - Minimize button: Marks window `WindowState::Minimized` and hides client surface.
- **Window Border**: 1-pixel accent border denoting focused (active) vs unfocused (inactive) state.

## 5. Concurrency & Thread-Safety
All surface reads and window lookups acquire the `WindowManager` mutex lock, ensuring no window can be destroyed or resized mid-composition pass.
