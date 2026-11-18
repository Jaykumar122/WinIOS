# Campaign 226: Desktop Interaction, Window Dragging & Notification Overlay

## 1. Overview
Stage 226.8 through 226.11 integrated interactive window dragging, modal overlays, notification toast rendering, and the unified desktop event loop inside `DesktopController`.

## 2. Interactive Window Dragging
Window dragging is handled interactively via pointer hit-testing and position tracking:
1. **Drag Initiation**:
   - A `PointerDown` event in `NonClientZone::TitleBar` records the drag anchor offset:
     $$\Delta x = \text{cursor}_x - \text{window}_x, \quad \Delta y = \text{cursor}_y - \text{window}_y$$
   - The dragging state is flagged as active for the window handle.
2. **Real-time Drag Tracking**:
   - As `PointerMove` occurs, the controller recalculates new window coordinates:
     $$\text{window}_x' = \text{cursor}_x - \Delta x, \quad \text{window}_y' = \text{cursor}_y - \Delta y$$
   - Coordinates are clamped to desktop display bounds so windows cannot be dragged entirely off-screen:
     $$x' \in [0, \text{disp\_w} - 40], \quad y' \in [26, \text{disp\_h} - 40]$$
3. **Drag Termination**:
   - On `PointerUp`, dragging state is cleared, and final window placement is committed.

## 3. Notification Overlay System
The desktop integrates with `VirtualNotificationService` to display active notifications as floating toasts:
- Up to 3 active notifications are rendered in the top-right corner of the desktop.
- Dimensions: 240 pixels wide, 50 pixels high, spaced by 10 pixels vertically.
- Color scheme: Dark slate background (`0x1C2230`) with a 4-pixel vertical colored accent bar denoting notification category:
  - System: Blue (`0x3C8CFF`)
  - Warning/Error: Red/Orange (`0xFF5C5C`)
  - User: Green/Cyan (`0x3CD2A0`)
- **Damage Awareness**: When toasts expire or are dismissed, `DesktopController::step()` detects the count change and triggers a full desktop redraw to clear toast ghosting from underlying window surfaces.

## 4. Desktop Event Loop & Ticking
`DesktopController::step()` serves as the central orchestration tick:
- Flushes pending pointer events from `VirtualPointerDevice` into hit-tested windows.
- Injects keyboard strokes from `VirtualKeyboardDevice` to the focused window.
- Expires outdated notifications in `VirtualNotificationService`.
- Triggers compositor passes with damage-based redraw optimization.
- Manages cooperative frame pacing at 60 Hz.
