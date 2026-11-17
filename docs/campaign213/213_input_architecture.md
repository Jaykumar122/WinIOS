# WinIOS Campaign 213 — Input Subsystem Architecture Specification

## 1. Subsystem Mission

The WinIOS Input Subsystem translates physical host mouse, keyboard, and touch events into virtual multi-touch digitizer interactions delivered deterministically to ARM64 guest processes executing at EL0.

The architecture eliminates host-side mock shortcuts: guest applications actively query input via validated syscalls (`SYS_winios_poll_event`, 504), process event structures in user memory, and update their visual state autonomously.

## 2. Event Structure and Protocol

The input system uses a 32-byte binary-aligned structure compatible with ARM64 Darwin userspace:

```cpp
struct GuestInputEvent {
    uint32_t type;         // 1=TouchBegin, 2=TouchMove, 3=TouchEnd, 4=TouchCancel, 5=Key, 7=Home, 8=Lock
    uint32_t contact_id;   // 0..9 (Multi-touch contact slot)
    int32_t  x;            // Virtual display coordinate X (0 .. width - 1)
    int32_t  y;            // Virtual display coordinate Y (0 .. height - 1)
    uint32_t key_code;     // Virtual key or button identifier
    uint32_t window_id;    // Destination window handle
    uint64_t timestamp_us; // Virtual clock timestamp in microseconds
};
```

## 3. Host-to-Virtual Coordinate Mapping

The Windows host desktop window footprint (typically scaled to 35%–50% of Retina resolution to fit standard desktop displays) is mapped to native virtual display coordinates using proportional scaling:

$$X_{virt} = \text{clamp}\left(\left\lfloor X_{host} \times \frac{W_{virt}}{W_{host}} \right\rfloor, 0, W_{virt} - 1\right)$$
$$Y_{virt} = \text{clamp}\left(\left\lfloor Y_{host} \times \frac{H_{virt}}{H_{host}} \right\rfloor, 0, H_{virt} - 1\right)$$

All input coordinates undergo bounds clamping to prevent integer underflow, overflow, or out-of-bounds targeting.

## 4. Hit-Testing, Focus, and Touch Contact Capture Policy

The input router enforces a deterministic pointer capture policy:

1. **TouchBegin Hit-Testing**:
   - The router queries `WindowManager::hit_test(x, y)`.
   - Windows are tested in descending Z-order (topmost visible window first).
   - If a window contains the point, that window and its owning PID capture the contact slot (`contact_id` $\in [0, 9]$).
   - The clicked window is automatically elevated to active focus.
   - If no window is hit, the event routes to PID 1 (Shell).

2. **TouchMove and TouchEnd Capture Retention**:
   - Even if subsequent finger/mouse movements drift outside the window frame, events for that `contact_id` remain exclusively routed to the process that captured the contact on `TouchBegin`.
   - On `TouchEnd` or `TouchCancel`, the capture record is released.

3. **Window Destruction Safety**:
   - If a window is destroyed while holding an active touch capture, `InputRouter::on_window_destroyed` synthesizes a `TouchCancel` event to the owning process and immediately frees the contact slot.

4. **Per-Process Queue Isolation**:
   - Each process possesses an independent, bounded FIFO `TouchEventQueue` (default capacity: 256 events).
   - Process A cannot inspect, poll, or drain events addressed to Process B.
   - Ring-buffer overflow safely drops oldest unhandled events and increments overflow diagnostics without unbounded heap allocations.
