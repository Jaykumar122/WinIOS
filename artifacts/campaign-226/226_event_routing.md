# Campaign 226: Input Event Routing & Focus Management

## 1. Overview
Input routing in WinIOS Campaign 226 bridges physical or simulated host input events into isolated per-window FIFO event queues. The `DesktopController` coordinates between `VirtualPointerDevice`, `VirtualKeyboardDevice`, and `WindowManager` to dispatch events to the correct recipient.

## 2. Pointer Event Dispatch & Hit Testing
When pointer movement, button down, or button up events occur:
1. **Screen-Space Hit Testing**:
   - The desktop controller queries `WindowManager::hit_test_non_client(x, y)` from top of Z-stack down.
   - Non-client zones include:
     - `NonClientZone::TitleBar`: Initiates window dragging if button 0 (left click) is pressed.
     - `NonClientZone::CloseButton`: Dispatches a `DesktopEventType::WindowCloseRequest` event to the target window's queue.
     - `NonClientZone::MaximizeButton`: Toggles `WindowState::Maximized` and `WindowState::Normal`.
     - `NonClientZone::MinimizeButton`: Sets `WindowState::Minimized`.
2. **Client Area Hit Testing**:
   - If the cursor falls inside the client area of a visible window, the window is brought to front (focus transfer) and activated.
   - Relative client coordinates $(x - w.x, y - w.y - \text{titlebar\_height})$ are computed and clamped.
   - A `DesktopEventType::PointerMove`, `PointerDown`, or `PointerUp` event is delivered to the window's `WindowEventQueue`.

## 3. Pointer Move Coalescing
High-frequency pointer moves can easily overwhelm bounded event queues:
- The `WindowEventQueue` automatically coalesces consecutive `PointerMove` events.
- If the newest event in the queue is already a `PointerMove` with identical button state, the queue updates the coordinates and timestamp in place rather than inserting an additional event slot.
- This guarantees that fast mouse drags never drop discrete `PointerDown`/`PointerUp` events or exhaust the 128-slot FIFO buffer.

## 4. Keyboard Routing & Focus Semantics
- Keyboard input follows a strict focus model: **only the top active window receives key events**.
- When `VirtualKeyboardDevice` reports a key press or release, `DesktopController::handle_key_event` looks up `WindowManager::get_active_window()`.
- If an active window exists, a `DesktopEventType::KeyDown` or `KeyUp` event is formatted containing:
  - `key_code` (e.g. ASCII / virtual key code).
  - `modifiers` bitmask (Shift, Ctrl, Alt, Meta).
  - Microsecond timestamp.
- If no window is active or the active window is closed, keyboard events are dropped safely without kernel faults.

## 5. Text Input Injection & Unicode Support
- Syscall `SYS_WINIOS_TEXT_INPUT_SUBMIT` (568) and `DesktopController::handle_text_input` allow submitting formatted text strings (up to 256 bytes) directly to the focused window.
- The controller emits a `DesktopEventType::TextInput` event containing the null-terminated UTF-8 character buffer.
- This decouples physical key scancodes from international text composition.
