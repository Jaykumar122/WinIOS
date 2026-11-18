# WinIOS Campaign 226 — Stage 226.2 Desktop Window-System Architecture

## 1. Architectural Mission & Principles
Campaign 226 elevates WinIOS from individual window abstractions into a coherent, multi-window desktop environment with verified input routing, deterministic composition, and rigorous process isolation.

The window system architecture adheres to six foundational principles:
1. **PID-Scoped Window Ownership:** Every window is strictly owned by the creating process PID (`owner_pid`). Cross-process window mutations, destruction, or focus theft without explicit permission are strictly denied with `EPERM`.
2. **64-bit Generation-Tagged Handles:** Window handles incorporate a 32-bit generation counter:
   $$\text{handle} = (\text{generation} \ll 32) \mid \text{window\_id}$$
   Any access via a stale handle (e.g. after a window has been closed and its ID recycled) is rejected with `EBADF` / `EINVAL`.
3. **Deterministic Stacking & Z-Order:** Windows maintain explicit signed 32-bit Z-indices. The compositor renders from lowest to highest Z-order, with `always_on_top` windows partitioned into a dedicated elevated layer.
4. **Per-Window Bounded Event Queues:** Each window encapsulates an independent FIFO event queue bounded to 128 events, preventing cross-application head-of-line blocking and memory exhaustion.
5. **Damage Tracking & Partial Redraw:** Window surfaces track invalidation rectangles, enabling the compositor to redraw only damaged regions or fallback to full-frame composition when damage lists overflow.
6. **Defensive Input Capture & Teardown:** Destruction or deactivation of a window automatically releases pointer capture and clears active key matrices, preventing stuck drag or key-repeat states.

---

## 2. Window Handle & Identity Model

```cpp
inline uint64_t make_window_handle(uint32_t id, uint32_t gen) noexcept {
    return (static_cast<uint64_t>(gen) << 32) | static_cast<uint64_t>(id);
}
inline uint32_t window_handle_id(uint64_t handle) noexcept {
    return static_cast<uint32_t>(handle & 0xFFFFFFFFULL);
}
inline uint32_t window_handle_gen(uint64_t handle) noexcept {
    return static_cast<uint32_t>((handle >> 32) & 0xFFFFFFFFULL);
}
```

### Stale Handle Defense:
When a window is closed:
1. Its state transitions to `WindowState::Closed`.
2. The internal generation counter in `WindowManager::generations_[id]` is incremented.
3. Stale handles referencing older generation values immediately fail lookup.

---

## 3. Window Lifecycle State Machine

```
              [Created]
                  │
             show() / map()
                  ▼
   ┌───────► [Visible] ◄────────┐
   │         ╱    │    ╲        │
restore() minimize() maximize() │
   │       ▼      │      ▼      │
[Minimized]       │   [Maximized]
   │              ▼             │
   └─────────► [Hidden] ◄───────┘
                  │
               close()
                  ▼
              [Closed]
```

### State Transitions & Rules:
- `Created`: Initial allocation state; surface initialized, not yet submitted for composition.
- `Visible`: Window surface is composed into desktop framebuffer in Z-order; eligible for pointer and keyboard focus.
- `Minimized`: Window is unmapped from desktop composition pass; remains in taskbar / dock.
- `Maximized`: Frame expands to cover available screen safe area; preserves pre-maximized restore geometry.
- `Hidden`: Window is hidden from desktop and dock; surface retained.
- `Closed`: Window is unmapped; surfaces, damage rects, and event queues are purged; handle invalidated.

---

## 4. Multi-Layer Stacking & Z-Order Management

The desktop compositor stacks surfaces across three deterministic Z-layers:
1. **Desktop / Shell Layer:** Z-order $\le -1000$ (Wallpaper, desktop icons).
2. **Normal Window Layer:** Z-order $\in [-999, 9999]$ (Standard application windows).
3. **Topmost / Modal Layer:** Z-order $\ge 10000$ (Always-on-top windows, modal dialogs, context menus).
4. **Overlay / Notification Layer:** Z-order $\ge 20000$ (System toast banners, cursor overlay).

When `raise_to_top()` is invoked on a normal window:
- Its Z-order is set to $\max(Z_{\text{normal}}) + 1 \le 9999$.
- The window becomes the active focus target.

---

## 5. Event Routing & Per-Window Bounded Queues

Every window owns a `WindowEventQueue`:
- **Capacity:** Fixed at $128$ events (`kMaxQueueEvents = 128`).
- **Event Types:**
  - `PointerMove`, `PointerDown`, `PointerUp`, `PointerWheel`
  - `KeyDown`, `KeyUp`, `TextInput`
  - `FocusIn`, `FocusOut`
  - `WindowMove`, `WindowResize`, `WindowCloseRequest`
- **Coalescing Policy:** Rapid consecutive `PointerMove` events with identical button masks are coalesced into a single event updating the coordinates, preventing queue starvation.
- **Queue Overflow Policy:** When non-coalescible events overflow 128 items, the oldest event is dropped and `overflow_drop_count_` is incremented.

---

## 6. Thread Safety & Synchronization
- `WindowManager` is synchronized with an internal `std::recursive_mutex`, ensuring consistent read/write access across host UI threads and guest syscall worker threads.
- Copyin/copyout operations to guest address space are executed outside the lock where possible to minimize lock contention.
