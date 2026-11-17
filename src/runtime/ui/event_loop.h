#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace winios::runtime::ui {

enum class UiEventType : std::uint8_t {
    Touch,
    Button,
    Focus,
    Lifecycle,
    Timer,
    Invalidate,
    Shutdown
};

struct UiEvent {
    UiEventType type{UiEventType::Touch};
    std::uint32_t window_id{0};
    std::int32_t x{0};
    std::int32_t y{0};
    std::uint32_t action_code{0}; // Sub-action: e.g. TouchBegin(1), TouchMove(2), TouchEnd(3)
    std::uint64_t timestamp_us{0};
    std::string string_payload;
    std::function<void()> callback;
};

class UiEventLoop {
public:
    explicit UiEventLoop(std::size_t max_queue_capacity = 256);

    bool post_event(const UiEvent& event);
    bool post_task(std::function<void()> task, std::uint64_t timestamp_us = 0);

    bool process_one();
    std::size_t run_until_idle();

    void request_shutdown() noexcept { running_ = false; }
    [[nodiscard]] bool is_running() const noexcept { return running_; }

    [[nodiscard]] std::size_t pending_event_count() const noexcept { return queue_.size(); }
    [[nodiscard]] std::uint64_t total_processed_events() const noexcept { return total_processed_; }
    [[nodiscard]] std::uint64_t total_dropped_events() const noexcept { return total_dropped_; }

    void set_event_handler(std::function<void(const UiEvent&)> handler) {
        event_handler_ = std::move(handler);
    }

private:
    std::size_t max_capacity_{256};
    std::deque<UiEvent> queue_;
    bool running_{true};
    std::uint64_t total_processed_{0};
    std::uint64_t total_dropped_{0};
    std::function<void(const UiEvent&)> event_handler_;
};

} // namespace winios::runtime::ui
