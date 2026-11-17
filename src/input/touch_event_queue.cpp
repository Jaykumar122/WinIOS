#include "input/touch_event_queue.h"

namespace winios::input {

TouchEventQueue::TouchEventQueue(std::size_t capacity)
    : capacity_(capacity == 0 ? kDefaultCapacity : capacity) {
    buffer_.resize(capacity_);
}

bool TouchEventQueue::push(const InputEvent& event) {
    std::lock_guard<std::mutex> guard(lock_);
    total_pushed_++;
    last_timestamp_us_ = event.timestamp_us;

    if (count_ >= capacity_) {
        // Queue full: overwrite oldest item (advance head)
        head_ = (head_ + 1) % capacity_;
        count_--;
        overflow_count_++;
    }

    buffer_[tail_] = event;
    tail_ = (tail_ + 1) % capacity_;
    count_++;
    return true;
}

bool TouchEventQueue::pop(InputEvent& out_event) {
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

bool TouchEventQueue::peek(InputEvent& out_event) const {
    std::lock_guard<std::mutex> guard(lock_);
    if (count_ == 0) {
        return false;
    }
    out_event = buffer_[head_];
    return true;
}

void TouchEventQueue::clear() {
    std::lock_guard<std::mutex> guard(lock_);
    head_ = 0;
    tail_ = 0;
    count_ = 0;
}

std::size_t TouchEventQueue::size() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return count_;
}

bool TouchEventQueue::empty() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return count_ == 0;
}

bool TouchEventQueue::full() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return count_ >= capacity_;
}

std::uint64_t TouchEventQueue::overflow_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return overflow_count_;
}

std::uint64_t TouchEventQueue::total_pushed() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return total_pushed_;
}

std::uint64_t TouchEventQueue::total_popped() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return total_popped_;
}

} // namespace winios::input
