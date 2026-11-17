#include "input/host_input_queue.h"

#include <algorithm>

namespace winios::input {

HostInputQueue::HostInputQueue(std::size_t capacity, QueueDropPolicy policy)
    : capacity_(capacity == 0 ? kDefaultCapacity : capacity)
    , policy_(policy) {
    buffer_.resize(capacity_);
}

bool HostInputQueue::push(const InputEvent& event) {
    std::lock_guard<std::mutex> guard(lock_);
    total_pushed_++;
    last_timestamp_us_ = event.timestamp_us;

    if (count_ >= capacity_) {
        total_dropped_++;
        if (policy_ == QueueDropPolicy::DropNewest) {
            // Drop newest incoming event
            return false;
        }
        // Drop oldest: advance head to discard front
        head_ = (head_ + 1) % capacity_;
        count_--;
    }

    buffer_[tail_] = event;
    tail_ = (tail_ + 1) % capacity_;
    count_++;
    return true;
}

bool HostInputQueue::pop(InputEvent& out_event) {
    std::lock_guard<std::mutex> guard(lock_);
    if (count_ == 0) {
        return false;
    }

    out_event = buffer_[head_];
    head_ = (head_ + 1) % capacity_;
    count_--;
    total_popped_++;
    return true;
}

bool HostInputQueue::peek(InputEvent& out_event) const {
    std::lock_guard<std::mutex> guard(lock_);
    if (count_ == 0) {
        return false;
    }
    out_event = buffer_[head_];
    return true;
}

std::size_t HostInputQueue::drain(std::vector<InputEvent>& out_events, std::size_t max_events) {
    std::lock_guard<std::mutex> guard(lock_);
    const std::size_t to_drain = std::min(count_, max_events);
    out_events.reserve(out_events.size() + to_drain);

    for (std::size_t i = 0; i < to_drain; ++i) {
        out_events.push_back(buffer_[head_]);
        head_ = (head_ + 1) % capacity_;
        count_--;
        total_popped_++;
    }
    return to_drain;
}

void HostInputQueue::clear() {
    std::lock_guard<std::mutex> guard(lock_);
    head_ = 0;
    tail_ = 0;
    count_ = 0;
}

std::size_t HostInputQueue::size() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return count_;
}

bool HostInputQueue::empty() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return count_ == 0;
}

bool HostInputQueue::full() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return count_ >= capacity_;
}

std::uint64_t HostInputQueue::dropped_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return total_dropped_;
}

QueueStats HostInputQueue::stats() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    QueueStats s{};
    s.count = count_;
    s.capacity = capacity_;
    s.total_pushed = total_pushed_;
    s.total_popped = total_popped_;
    s.total_dropped = total_dropped_;
    s.last_timestamp_us = last_timestamp_us_;
    return s;
}

} // namespace winios::input
