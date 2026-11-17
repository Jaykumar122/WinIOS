#include "runtime/ui/event_loop.h"

namespace winios::runtime::ui {

UiEventLoop::UiEventLoop(std::size_t max_queue_capacity)
    : max_capacity_(max_queue_capacity) {}

bool UiEventLoop::post_event(const UiEvent& event) {
    if (queue_.size() >= max_capacity_) {
        ++total_dropped_;
        return false;
    }
    queue_.push_back(event);
    return true;
}

bool UiEventLoop::post_task(std::function<void()> task, std::uint64_t timestamp_us) {
    if (!task) return false;
    UiEvent evt{};
    evt.type = UiEventType::Timer;
    evt.timestamp_us = timestamp_us;
    evt.callback = std::move(task);
    return post_event(evt);
}

bool UiEventLoop::process_one() {
    if (!running_ || queue_.empty()) return false;

    UiEvent evt = std::move(queue_.front());
    queue_.pop_front();
    ++total_processed_;

    if (evt.type == UiEventType::Shutdown) {
        running_ = false;
        return false;
    }

    if (evt.callback) {
        evt.callback();
    }

    if (event_handler_) {
        event_handler_(evt);
    }

    return true;
}

std::size_t UiEventLoop::run_until_idle() {
    std::size_t processed = 0;
    while (running_ && !queue_.empty()) {
        if (process_one()) {
            ++processed;
        } else {
            break;
        }
    }
    return processed;
}

} // namespace winios::runtime::ui
