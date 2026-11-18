#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <cstring>

namespace winios::graphics {

// -----------------------------------------------------------------------------
// Desktop Event Types (ABI v2.0)
// -----------------------------------------------------------------------------
enum class DesktopEventType : std::uint32_t {
    None                = 0,
    PointerMove         = 1,
    PointerDown         = 2,
    PointerUp           = 3,
    PointerWheel        = 4,
    KeyDown             = 5,
    KeyUp               = 6,
    TextInput           = 7,
    FocusGained         = 8,
    FocusLost           = 9,
    WindowMove          = 10,
    WindowResize        = 11,
    WindowCloseRequest  = 12,
    WindowStateChanged  = 13,
    DisplayReconfigured = 14
};

#pragma pack(push, 8)

struct DesktopEvent {
    std::uint32_t  event_type{0};       // DesktopEventType
    std::uint32_t  flags{0};            // Event flags (e.g. synthetic, repeat)
    std::uint64_t  timestamp_us{0};     // Monotonic timestamp
    std::uint64_t  window_handle{0};    // Target 64-bit generation window handle
    std::int32_t   x{0};                // Coordinates relative to window client area
    std::int32_t   y{0};
    std::int32_t   screen_x{0};         // Absolute display coordinates
    std::int32_t   screen_y{0};
    std::uint32_t  button_or_key{0};    // Mouse button (1:L, 2:R, 3:M) or virtual key code
    std::int32_t   wheel_delta{0};      // Scroll wheel delta
    std::uint32_t  modifiers{0};        // Shift (1), Ctrl (2), Alt (4), Meta (8)
    std::uint32_t  payload_len{0};      // Length of UTF-8 or auxiliary payload
    std::uint8_t   payload[64]{0};      // Inline payload (e.g. text characters)
};

#pragma pack(pop)

// -----------------------------------------------------------------------------
// WindowEventQueue: Thread-Safe Bounded Event Queue (Capacity 128)
// -----------------------------------------------------------------------------
class WindowEventQueue {
public:
    static constexpr std::size_t kDefaultCapacity = 128;

    explicit WindowEventQueue(std::size_t capacity = kDefaultCapacity)
        : capacity_(capacity) {}

    ~WindowEventQueue() = default;

    WindowEventQueue(const WindowEventQueue&) = delete;
    WindowEventQueue& operator=(const WindowEventQueue&) = delete;

    // Enqueue event with coalescing for rapid pointer movements
    bool push(const DesktopEvent& ev) {
        std::lock_guard<std::mutex> guard(lock_);

        // Coalesce mouse move events if target window and button mask match
        if (ev.event_type == static_cast<std::uint32_t>(DesktopEventType::PointerMove) && !queue_.empty()) {
            DesktopEvent& last = queue_.back();
            if (last.event_type == static_cast<std::uint32_t>(DesktopEventType::PointerMove) &&
                last.window_handle == ev.window_handle &&
                last.button_or_key == ev.button_or_key &&
                last.modifiers == ev.modifiers) {
                last.x = ev.x;
                last.y = ev.y;
                last.screen_x = ev.screen_x;
                last.screen_y = ev.screen_y;
                last.timestamp_us = ev.timestamp_us;
                ++coalesced_count_;
                return true;
            }
        }

        // Bounded queue overflow policy
        if (queue_.size() >= capacity_) {
            queue_.pop_front();
            ++dropped_count_;
        }

        queue_.push_back(ev);
        ++total_pushed_;
        return true;
    }

    bool pop(DesktopEvent& out_event) {
        std::lock_guard<std::mutex> guard(lock_);
        if (queue_.empty()) {
            return false;
        }
        out_event = queue_.front();
        queue_.pop_front();
        ++total_popped_;
        return true;
    }

    bool peek(DesktopEvent& out_event) const {
        std::lock_guard<std::mutex> guard(lock_);
        if (queue_.empty()) {
            return false;
        }
        out_event = queue_.front();
        return true;
    }

    void clear() {
        std::lock_guard<std::mutex> guard(lock_);
        queue_.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        std::lock_guard<std::mutex> guard(lock_);
        return queue_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard<std::mutex> guard(lock_);
        return queue_.empty();
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::uint64_t total_pushed() const noexcept { return total_pushed_; }
    [[nodiscard]] std::uint64_t total_popped() const noexcept { return total_popped_; }
    [[nodiscard]] std::uint64_t dropped_count() const noexcept { return dropped_count_; }
    [[nodiscard]] std::uint64_t coalesced_count() const noexcept { return coalesced_count_; }

private:
    mutable std::mutex       lock_;
    std::size_t              capacity_{kDefaultCapacity};
    std::deque<DesktopEvent> queue_;
    std::uint64_t            total_pushed_{0};
    std::uint64_t            total_popped_{0};
    std::uint64_t            dropped_count_{0};
    std::uint64_t            coalesced_count_{0};
};

} // namespace winios::graphics
